#!/usr/bin/env python3
"""Validate the recipe, item-tag and fuel data.

Standard library only.  Usage:

    python3 -I tools/check_recipes.py [--uncraftable] [--dump]

The files are loaded the way the engine is specified to load them:

* data/recipes/*.json hold {"recipes": [...]}.  data/recipes/families.json holds
  {"families": [{"name", "rows", "recipes"}]}: every template in "recipes" is expanded once
  per row, "{var}" being replaced by the row's value in every string value.  A template that
  uses a variable the row does not define is skipped for that row.  Keys starting with "_"
  are ignored everywhere.
* data/tags/items/**.json are item tags, {"values": [...], "exclude": [...]}.  Entries are
  item ids, '*' globs or "#tag" references (nested; a sub-folder names "dir/tag").
* data/items/fuel.json is {"rules": [{"match": [...], "ticks": n}]}; the first rule that
  matches an item gives its burn time.

Item ids are the file names in the resource pack's items/ folder.

PROBLEMS (exit status 1):
* unreadable JSON and duplicate JSON keys
* unknown item ids, unknown or empty tags, globs that match nothing, tag cycles
* malformed recipes: type, pattern shape, key symbols unused or undefined, empty edge
  rows/columns, ingredient counts, result counts above the stack size, cooking times other
  than vanilla's, campfire experience other than 0
* crafting conflicts: two recipes that can match the same grid (mirroring included) with
  different results.  Cooking conflicts: same appliance, overlapping inputs
* duplicate recipes: identical, or overlapping inputs with the same result
* fuel rules that match nothing, or only items an earlier rule already claims

WARNINGS are softer findings, such as a blasting recipe without a smelting twin.  Totals are
printed per recipe type, per file and per family.

  --uncraftable  also list the pack's items that no recipe produces
  --dump         print every expanded recipe as one JSON line (with its origin) and exit
"""

import json
import os
import re
import sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACK_ITEMS = os.path.join(ROOT, "Java Default Textures 1.21.7 - 1.21.8", "assets", "minecraft", "items")
RECIPE_DIR = os.path.join(ROOT, "data", "recipes")
FAMILIES_FILE = "families.json"
TAG_DIR = os.path.join(ROOT, "data", "tags", "items")
FUEL_FILE = os.path.join(ROOT, "data", "items", "fuel.json")
ITEMS_FILE = os.path.join(ROOT, "data", "items", "items.json")

CRAFTING = ("shaped", "shapeless")
COOKING_TIME = {"smelting": 200, "blasting": 100, "smoking": 100, "campfire_cooking": 600}
TYPES = CRAFTING + tuple(COOKING_TIME)
RECIPE_KEYS = {
    "shaped": {"type", "pattern", "key", "result"},
    "shapeless": {"type", "ingredients", "result"},
}
for _t in COOKING_TIME:
    RECIPE_KEYS[_t] = {"type", "ingredient", "result", "experience", "cookingtime"}

# Overlapping crafting recipes that vanilla 1.21.8 has as well: listed, not reported.
# Keyed by the sorted pair of result ids, valued with the reason.
VANILLA_OVERLAPS = {}

PLACEHOLDER = re.compile(r"\{([A-Za-z0-9_]+)\}")

problems = []  # (kind, message)
warnings = []


def rel(path):
    return os.path.relpath(path, ROOT)


def problem(kind, msg):
    problems.append((kind, msg))


def warn(msg):
    warnings.append(msg)


def load_json(path):
    where = rel(path)

    def hook(pairs):
        seen = set()
        for k, _ in pairs:
            if k in seen:
                problem("json", f"{where}: duplicate key {k!r}")
            seen.add(k)
        return dict(pairs)

    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f, object_pairs_hook=hook)
    except OSError as e:
        problem("json", f"{where}: cannot read: {e}")
    except ValueError as e:
        problem("json", f"{where}: invalid JSON: {e}")
    return None


_globs = {}


def glob_match(pattern, text):
    rx = _globs.get(pattern)
    if rx is None:
        rx = _globs[pattern] = re.compile(".*".join(map(re.escape, pattern.split("*"))))
    return rx.fullmatch(text) is not None


# ----------------------------------------------------------------------------- items, tags


def load_items():
    try:
        return sorted(f[:-5] for f in os.listdir(PACK_ITEMS) if f.endswith(".json"))
    except OSError as e:
        sys.exit(f"cannot list the pack's items in {PACK_ITEMS}: {e}")


def load_stack_sizes(items):
    """Stack size per item from data/items/items.json's stack_rules (first match, default 64)."""
    rules = []
    try:
        with open(ITEMS_FILE, encoding="utf-8") as f:
            rules = json.load(f).get("stack_rules", [])
    except (OSError, ValueError, AttributeError):
        warn(f"{rel(ITEMS_FILE)} unreadable; result counts are checked against 64 only")
    sizes = {}
    for item in items:
        sizes[item] = 64
        for rule in rules:
            if any(isinstance(m, str) and glob_match(m, item) for m in rule.get("match", [])):
                sizes[item] = rule.get("stack", 64)
                break
    return sizes


class ItemTags:
    def __init__(self, items):
        self.items = items
        self.item_set = set(items)
        self.raw = {}  # name -> {"values", "exclude", "file"}
        self.done = {}
        self.busy = set()
        self.used = set()  # referenced from recipes, fuel rules or other tags
        if not os.path.isdir(TAG_DIR):
            problem("tags", f"missing directory {rel(TAG_DIR)}")
            return
        for dirpath, dirnames, files in os.walk(TAG_DIR):
            dirnames.sort()
            for fn in sorted(files):
                path = os.path.join(dirpath, fn)
                if not fn.endswith(".json"):
                    warn(f"{rel(path)}: not a .json file, ignored")
                    continue
                name = os.path.relpath(path, TAG_DIR)[:-5].replace(os.sep, "/")
                doc = load_json(path)
                if doc is None:
                    continue
                if not isinstance(doc, dict):
                    problem("tags", f"{rel(path)}: not a JSON object")
                    continue
                entry = {"values": [], "exclude": [], "file": rel(path)}
                if "values" not in doc:
                    problem("tags", f"{rel(path)}: no \"values\" list")
                for key, val in doc.items():
                    if key.startswith("_"):
                        continue
                    if key not in ("values", "exclude"):
                        problem("tags", f"{rel(path)}: unknown key {key!r}")
                    elif not isinstance(val, list) or not all(isinstance(v, str) for v in val):
                        problem("tags", f"{rel(path)}: {key!r} must be a list of strings")
                    else:
                        entry[key] = val
                        repeated = sorted(v for v, n in Counter(val).items() if n > 1)
                        if repeated:
                            warn(f"{rel(path)}: repeated {key} entries {repeated}")
                self.raw[name] = entry

    def exists(self, name):
        return name in self.raw

    def resolve(self, name):
        if name in self.done:
            return self.done[name]
        if name in self.busy:
            problem("tags", f"#{name} is part of a reference cycle")
            return frozenset()
        self.busy.add(name)
        raw = self.raw[name]
        got = set()
        for v in raw["values"]:
            got |= self.entry(v, f"{raw['file']} values")
        for v in raw["exclude"]:
            got -= self.entry(v, f"{raw['file']} exclude")
        self.busy.discard(name)
        self.done[name] = frozenset(got)
        return self.done[name]

    def entry(self, e, where):
        """Items one tag/fuel entry stands for (id, glob or #tag)."""
        if "minecraft:" in e:
            problem("malformed", f"{where}: {e!r} is namespaced; ids are written without 'minecraft:'")
            e = e.replace("minecraft:", "", 1)
        if e.startswith("#"):
            ref = e[1:]
            if ref not in self.raw:
                problem("unknown tag", f"{where}: {e}")
                return set()
            self.used.add(ref)
            return set(self.resolve(ref))
        if "*" in e:
            got = {i for i in self.items if glob_match(e, i)}
            if not got:
                problem("tags", f"{where}: glob {e!r} matches no item")
            return got
        if e not in self.item_set:
            problem("unknown item", f"{where}: {e}")
            return set()
        return {e}

    def check_all(self):
        for name in sorted(self.raw):
            if not self.resolve(name):
                problem("empty tag", f"{self.raw[name]['file']}: #{name} resolves to no item")


# ----------------------------------------------------------------------------- recipes


class Recipe:
    """A loaded recipe; for crafting, `cells` holds (canonical text, item set) or None per cell."""

    def __init__(self, origin, rtype, raw):
        self.origin = origin
        self.type = rtype
        self.raw = raw
        self.ok = True
        self.result = None
        self.count = 1
        self.w = self.h = 0
        self.cells = []  # shaped, row-major
        self.ings = []  # shapeless list / cooking single, (canon, set)
        self.xp = 0
        self.time = 0

    def label(self):
        return f"{self.origin} -> {self.result}" + (f" x{self.count}" if self.count != 1 else "")

    def filled(self):
        if self.type == "shaped":
            return [c for c in self.cells if c is not None]
        return self.ings

    def mirrored_cells(self):
        rows = [self.cells[r * self.w:(r + 1) * self.w] for r in range(self.h)]
        return [c for row in rows for c in reversed(row)]

    def canon(self):
        """Identity of the inputs (mirror-normalised for shaped recipes)."""
        if self.type == "shaped":
            a = tuple(c[0] if c else None for c in self.cells)
            b = tuple(c[0] if c else None for c in self.mirrored_cells())
            return ("crafting", self.w, self.h, min(a, b, key=repr))
        if self.type == "shapeless":
            return ("crafting", tuple(sorted(c[0] for c in self.ings)))
        return (self.type, self.ings[0][0])


class Loader:
    def __init__(self, items, tags, stack):
        self.items = items
        self.item_set = set(items)
        self.tags = tags
        self.stack = stack
        self.family_stats = []  # (name, rows, templates, recipes)
        self.file_counts = Counter()

    # -- ingredients
    def ingredient(self, ing, where):
        """-> (canonical text, frozenset of items) or None."""
        if isinstance(ing, str):
            if "minecraft:" in ing:
                problem("malformed", f"{where}: {ing!r} is namespaced; ids are written without 'minecraft:'")
                ing = ing.replace("minecraft:", "", 1)
            if "{" in ing or "}" in ing:
                problem("malformed", f"{where}: unexpanded placeholder in {ing!r}")
                return None
            if ing.startswith("#"):
                name = ing[1:]
                if not self.tags.exists(name):
                    problem("unknown tag", f"{where}: {ing}")
                    return None
                self.tags.used.add(name)
                got = self.tags.resolve(name)
                if not got:
                    problem("empty tag", f"{where}: {ing} resolves to no item")
                    return None
                return ing, got
            if ing not in self.item_set or ing == "air":
                problem("unknown item", f"{where}: {ing}")
                return None
            return ing, frozenset((ing,))
        if isinstance(ing, list):
            if not ing:
                problem("malformed", f"{where}: empty ingredient list")
                return None
            got, ok = [], True
            for e in ing:
                if not isinstance(e, str):
                    problem("malformed", f"{where}: ingredient list entry {e!r} is not a string")
                    ok = False
                elif e.startswith("#"):
                    problem("malformed", f"{where}: tag {e} inside an ingredient list (lists hold item ids only)")
                    ok = False
                elif e not in self.item_set or e == "air":
                    problem("unknown item", f"{where}: {e}")
                    ok = False
                elif e in got:
                    problem("malformed", f"{where}: {e} repeated in an ingredient list")
                else:
                    got.append(e)
            if not ok or not got:
                return None
            if len(got) == 1:
                warn(f"{where}: one-item ingredient list {ing}; a plain string would do")
            return json.dumps(sorted(got)), frozenset(got)
        problem("malformed", f"{where}: an ingredient is an item id, a \"#tag\" or a list of item ids, "
                             f"not {json.dumps(ing)}")
        return None

    # -- one recipe
    def recipe(self, raw, origin):
        if not isinstance(raw, dict):
            problem("malformed", f"{origin}: a recipe must be a JSON object")
            return None
        rtype = raw.get("type")
        if rtype not in TYPES:
            problem("malformed", f"{origin}: unknown recipe type {rtype!r}")
            return None
        rec = Recipe(origin, rtype, raw)
        allowed = RECIPE_KEYS[rtype]
        for k in raw:
            if not k.startswith("_") and k not in allowed:
                problem("malformed", f"{origin}: unexpected key {k!r} in a {rtype} recipe")
        for k in sorted(allowed - raw.keys()):
            problem("malformed", f"{origin}: missing {k!r}")
            rec.ok = False
        self.result(rec, raw.get("result"))
        if rtype == "shaped":
            self.shaped(rec, raw.get("pattern"), raw.get("key"))
        elif rtype == "shapeless":
            self.shapeless(rec, raw.get("ingredients"))
        else:
            self.cooking(rec, raw)
        return rec

    def result(self, rec, res):
        where = rec.origin
        if not isinstance(res, dict):
            problem("malformed", f"{where}: result must be {{\"id\": ..., \"count\": n}}")
            rec.ok = False
            return
        for k in res:
            if not k.startswith("_") and k not in ("id", "count"):
                problem("malformed", f"{where}: unexpected result key {k!r}")
        rid, count = res.get("id"), res.get("count", 1)
        if not isinstance(rid, str):
            problem("malformed", f"{where}: result id must be a string")
            rec.ok = False
            return
        if "{" in rid or "}" in rid:
            problem("malformed", f"{where}: unexpanded placeholder in result {rid!r}")
            rec.ok = False
            return
        rec.result = rid
        if rid not in self.item_set or rid == "air":
            problem("unknown item", f"{where}: result {rid}")
            rec.ok = False
        if not isinstance(count, int) or isinstance(count, bool) or count < 1:
            problem("malformed", f"{where}: result count must be a positive integer, not {count!r}")
            rec.ok = False
            return
        rec.count = count
        if rid in self.stack and count > self.stack[rid]:
            problem("malformed", f"{where}: result count {count} exceeds {rid}'s stack size {self.stack[rid]}")
        if rec.type in COOKING_TIME and count != 1:
            warn(f"{where}: cooking result count {count} (vanilla cooking yields 1)")

    def shaped(self, rec, pattern, key):
        where = rec.origin
        if not (isinstance(pattern, list) and 1 <= len(pattern) <= 3 and all(isinstance(r, str) for r in pattern)):
            problem("malformed", f"{where}: pattern must be a list of 1-3 strings")
            rec.ok = False
            return
        widths = {len(r) for r in pattern}
        if len(widths) != 1:
            problem("malformed", f"{where}: pattern rows differ in length {pattern}")
            rec.ok = False
            return
        w, h = widths.pop(), len(pattern)
        if not 1 <= w <= 3:
            problem("malformed", f"{where}: pattern rows must be 1-3 characters, got {pattern}")
            rec.ok = False
            return
        if not isinstance(key, dict):
            problem("malformed", f"{where}: key must be an object")
            rec.ok = False
            return
        if pattern[0].strip() == "" or pattern[-1].strip() == "" or all(r[0] == " " for r in pattern) \
                or all(r[-1] == " " for r in pattern):
            problem("malformed", f"{where}: pattern {pattern} has an empty edge row or column")
        symbols = {}
        for sym, ing in key.items():
            if sym.startswith("_"):  # comments; so '_' itself cannot be a symbol
                continue
            if len(sym) != 1 or sym == " ":
                problem("malformed", f"{where}: key symbol {sym!r} must be one character other than space")
                rec.ok = False
                continue
            parsed = self.ingredient(ing, f"{where} key {sym!r}")
            if parsed is None:
                rec.ok = False
            symbols[sym] = parsed
        used = set("".join(pattern)) - {" "}
        for sym in sorted(used - symbols.keys()):
            note = " (keys starting with '_' are ignored)" if sym == "_" else ""
            problem("malformed", f"{where}: pattern symbol {sym!r} is not in the key{note}")
            rec.ok = False
        for sym in sorted(symbols.keys() - used):
            problem("malformed", f"{where}: key symbol {sym!r} is not used in the pattern")
        rec.w, rec.h = w, h
        rec.cells = [None if ch == " " else symbols.get(ch) for row in pattern for ch in row]
        if any(ch != " " and symbols.get(ch) is None for row in pattern for ch in row):
            rec.ok = False

    def shapeless(self, rec, ings):
        where = rec.origin
        if not isinstance(ings, list) or not 1 <= len(ings) <= 9:
            problem("malformed", f"{where}: ingredients must be a list of 1-9 ingredients")
            rec.ok = False
            return
        for i, ing in enumerate(ings):
            parsed = self.ingredient(ing, f"{where} ingredient {i}")
            if parsed is None:
                rec.ok = False
            else:
                rec.ings.append(parsed)

    def cooking(self, rec, raw):
        where = rec.origin
        parsed = self.ingredient(raw.get("ingredient"), f"{where} ingredient")
        if parsed is None:
            rec.ok = False
        else:
            rec.ings = [parsed]
        xp, time = raw.get("experience"), raw.get("cookingtime")
        if not isinstance(xp, (int, float)) or isinstance(xp, bool) or xp < 0:
            problem("malformed", f"{where}: experience must be a number >= 0, not {xp!r}")
        else:
            rec.xp = xp
            if rec.type == "campfire_cooking" and xp != 0:
                problem("malformed", f"{where}: campfire cooking gives no experience; got {xp}")
        if not isinstance(time, int) or isinstance(time, bool) or time <= 0:
            problem("malformed", f"{where}: cookingtime must be a positive integer, not {time!r}")
        else:
            rec.time = time
            if time != COOKING_TIME[rec.type]:
                problem("malformed", f"{where}: {rec.type} takes {COOKING_TIME[rec.type]} ticks in vanilla, not {time}")

    # -- families
    def expand_families(self, fname, doc):
        out = []
        fams = doc.get("families")
        if not isinstance(fams, list):
            problem("families", f"{fname}: \"families\" must be a list")
            return out
        names = Counter()
        for fi, fam in enumerate(fams):
            where = f"{fname}:families[{fi}]"
            if not isinstance(fam, dict):
                problem("families", f"{where}: a family must be an object")
                continue
            name = fam.get("name")
            if isinstance(name, str) and name:
                where = f"{fname}:{name}"
                names[name] += 1
            else:
                problem("families", f"{where}: missing \"name\"")
            for k in fam:
                if not k.startswith("_") and k not in ("name", "rows", "recipes"):
                    problem("families", f"{where}: unexpected key {k!r}")
            rows, temps = fam.get("rows"), fam.get("recipes")
            if not isinstance(rows, list) or not rows or not isinstance(temps, list) or not temps:
                problem("families", f"{where}: \"rows\" and \"recipes\" must be non-empty lists")
                continue
            tvars = []
            for ti, t in enumerate(temps):
                found, hidden = set(), set()
                collect_placeholders(t, found, hidden)
                tvars.append(found)
                if hidden:
                    warn(f"{where} template {ti}: placeholders {sorted(hidden)} inside '_' keys are not expanded")
                if not found:
                    warn(f"{where} template {ti} uses no variable, so it repeats once per row")
            applied = Counter()
            made = 0
            for ri, row in enumerate(rows):
                if not isinstance(row, dict):
                    problem("families", f"{where} row {ri}: a row must be an object")
                    continue
                bad = sorted(k for k, v in row.items() if not k.startswith("_") and not isinstance(v, str))
                if bad:
                    problem("families", f"{where} row {ri}: values of {bad} must be strings")
                    continue
                label = next((v for k, v in row.items() if not k.startswith("_")), str(ri))
                used = set()
                for ti, t in enumerate(temps):
                    if tvars[ti] <= row.keys():
                        out.append((f"{where}[{label}]#{ti}", substitute(t, row)))
                        applied[ti] += 1
                        used |= tvars[ti]
                        made += 1
                if not used and not any(tvars[ti] <= row.keys() for ti in range(len(temps))):
                    warn(f"{where} row {label}: no template applies")
                spare = sorted(k for k in row if not k.startswith("_") and k not in used)
                if spare:
                    warn(f"{where} row {label}: variables {spare} are used by no template that applies")
            for ti in range(len(temps)):
                if not applied[ti]:
                    warn(f"{where} template {ti} applies to no row")
            self.family_stats.append((name, len(rows), len(temps), made))
        for n, c in names.items():
            if c > 1:
                warn(f"{fname}: family name {n!r} used {c} times")
        return out

    def load(self):
        recs = []
        if not os.path.isdir(RECIPE_DIR):
            problem("recipes", f"missing directory {rel(RECIPE_DIR)}")
            return recs
        entries = sorted(os.listdir(RECIPE_DIR))
        for fn in entries:
            path = os.path.join(RECIPE_DIR, fn)
            if os.path.isdir(path):
                warn(f"{rel(path)}: sub-folders of data/recipes are not loaded")
                continue
            if not fn.endswith(".json"):
                warn(f"{rel(path)}: not a .json file, ignored")
                continue
            doc = load_json(path)
            if doc is None:
                continue
            if not isinstance(doc, dict):
                problem("recipes", f"{fn}: not a JSON object")
                continue
            keys = {k for k in doc if not k.startswith("_")}
            if fn == FAMILIES_FILE:
                if keys != {"families"}:
                    problem("recipes", f"{fn}: expected {{\"families\": [...]}}, got keys {sorted(keys)}")
                raw = self.expand_families(fn, doc)
            else:
                if keys != {"recipes"}:
                    problem("recipes", f"{fn}: expected {{\"recipes\": [...]}}, got keys {sorted(keys)}")
                lst = doc.get("recipes")
                if not isinstance(lst, list):
                    problem("recipes", f"{fn}: \"recipes\" must be a list")
                    continue
                raw = [(f"{fn}[{i}]", r) for i, r in enumerate(lst)]
            for origin, r in raw:
                rec = self.recipe(r, origin)
                if rec is not None:
                    recs.append(rec)
                    self.file_counts[fn] += 1
        return recs


def collect_placeholders(obj, found, hidden, in_hidden=False):
    if isinstance(obj, str):
        (hidden if in_hidden else found).update(PLACEHOLDER.findall(obj))
    elif isinstance(obj, list):
        for v in obj:
            collect_placeholders(v, found, hidden, in_hidden)
    elif isinstance(obj, dict):
        for k, v in obj.items():
            if PLACEHOLDER.search(k):
                warn(f"placeholder in key {k!r} is not expanded")
            collect_placeholders(v, found, hidden, in_hidden or k.startswith("_"))


def substitute(obj, row):
    if isinstance(obj, str):
        return PLACEHOLDER.sub(lambda m: row[m.group(1)], obj)
    if isinstance(obj, list):
        return [substitute(v, row) for v in obj]
    if isinstance(obj, dict):
        return {k: (v if k.startswith("_") else substitute(v, row)) for k, v in obj.items()}
    return obj


# ----------------------------------------------------------------------------- overlaps


def perfect_matching(left, right):
    """True when every left set can be paired with a distinct right set it intersects."""
    if len(left) != len(right):
        return False
    n = len(left)
    adj = [[j for j in range(n) if not left[i].isdisjoint(right[j])] for i in range(n)]
    owner = [-1] * n

    def augment(i, seen):
        for j in adj[i]:
            if j not in seen:
                seen.add(j)
                if owner[j] < 0 or augment(owner[j], seen):
                    owner[j] = i
                    return True
        return False

    return all(augment(i, set()) for i in range(n))


def cells_overlap(a, b):
    for x, y in zip(a, b):
        if (x is None) != (y is None):
            return False
        if x is not None and x[1].isdisjoint(y[1]):
            return False
    return True


def crafting_overlap(a, b):
    fa, fb = a.filled(), b.filled()
    if len(fa) != len(fb):
        return False
    if a.type == "shaped" and b.type == "shaped":
        if (a.w, a.h) != (b.w, b.h):
            return False
        return cells_overlap(a.cells, b.cells) or cells_overlap(a.cells, b.mirrored_cells())
    return perfect_matching([c[1] for c in fa], [c[1] for c in fb])


def report_pair(a, b, kind_same, kind_diff, known):
    if a.result == b.result:
        if a.canon() == b.canon() and a.count == b.count:
            problem("duplicate", f"identical recipes: {a.label()}  ==  {b.label()}")
        else:
            problem("duplicate", f"{kind_same}: {a.label()}  overlaps  {b.label()}")
        return
    pair = tuple(sorted((a.result, b.result)))
    if pair in VANILLA_OVERLAPS:
        known.append(f"{a.label()}  vs  {b.label()}  ({VANILLA_OVERLAPS[pair]})")
    else:
        problem("conflict", f"{kind_diff}: {a.label()}  vs  {b.label()}")


def find_overlaps(recs):
    known = []
    crafting = [r for r in recs if r.ok and r.type in CRAFTING]
    by_size = defaultdict(list)
    for r in crafting:
        by_size[len(r.filled())].append(r)
    for group in by_size.values():
        for i, a in enumerate(group):
            for b in group[i + 1:]:
                if crafting_overlap(a, b):
                    report_pair(a, b, "same grid, same result", "same grid, different results", known)
    by_type = defaultdict(list)
    for r in recs:
        if r.ok and r.type in COOKING_TIME:
            by_type[r.type].append(r)
    for rtype, group in by_type.items():
        for i, a in enumerate(group):
            for b in group[i + 1:]:
                if not a.ings[0][1].isdisjoint(b.ings[0][1]):
                    report_pair(a, b, f"{rtype} input claimed twice", f"{rtype} input claimed twice", known)
    return known


def check_cooking_twins(recs):
    """Vanilla pairs every blasting/smoking recipe with a smelting one, and campfire with smoking."""
    have = defaultdict(set)
    for r in recs:
        if r.ok and r.type in COOKING_TIME:
            for item in r.ings[0][1]:
                have[r.type].add((item, r.result))
    for r in recs:
        if not r.ok or r.type not in ("blasting", "smoking", "campfire_cooking"):
            continue
        twin = "smoking" if r.type == "campfire_cooking" else "smelting"
        missing = sorted(i for i in r.ings[0][1] if (i, r.result) not in have[twin])
        if missing:
            warn(f"{r.label()}: no {twin} recipe for {missing}")


# ----------------------------------------------------------------------------- fuel


def check_fuel(tags):
    burn = {}
    doc = load_json(FUEL_FILE)
    if doc is None:
        return burn
    if not isinstance(doc, dict) or not isinstance(doc.get("rules"), list):
        problem("fuel", "fuel.json must be {\"rules\": [...]}")
        return burn
    for k in doc:
        if not k.startswith("_") and k != "rules":
            problem("fuel", f"fuel.json: unexpected key {k!r}")
    for i, rule in enumerate(doc["rules"]):
        where = f"fuel.json rule {i}"
        if not isinstance(rule, dict):
            problem("fuel", f"{where}: a rule must be an object")
            continue
        for k in rule:
            if not k.startswith("_") and k not in ("match", "ticks"):
                problem("fuel", f"{where}: unexpected key {k!r}")
        match, ticks = rule.get("match"), rule.get("ticks")
        good_ticks = isinstance(ticks, int) and not isinstance(ticks, bool) and ticks > 0
        if not good_ticks:
            problem("fuel", f"{where}: ticks must be a positive integer, not {ticks!r}")
        if not isinstance(match, list) or not match or not all(isinstance(m, str) for m in match):
            problem("fuel", f"{where}: match must be a non-empty list of strings")
            continue
        where = f"{where} ({ticks} ticks)"
        got = set()
        for e in match:
            items = tags.entry(e, where)
            if e.startswith("#") and not items and tags.exists(e[1:]):
                problem("fuel", f"{where}: {e} resolves to no item")
            got |= items
        fresh = got - burn.keys()
        if got and not fresh:
            problem("fuel", f"{where}: every item it matches is claimed by an earlier rule")
        if good_ticks:
            for item in fresh:
                burn[item] = ticks
    if tags.exists("non_flammable_wood"):
        hot = sorted(set(tags.resolve("non_flammable_wood")) & burn.keys())
        if hot:
            warn(f"fuel: non-flammable wood burns: {hot}")
    return burn


# ----------------------------------------------------------------------------- main


def main(argv):
    flags = set(argv[1:])
    unknown = flags - {"--uncraftable", "--dump"}
    if unknown:
        sys.exit(f"unknown option(s) {sorted(unknown)}\n{__doc__}")
    items = load_items()
    tags = ItemTags(items)
    loader = Loader(items, tags, load_stack_sizes(items))
    recs = loader.load()

    if "--dump" in flags:
        for r in recs:
            print(json.dumps({"_origin": r.origin, **r.raw}, sort_keys=False))
        return 0

    tags.check_all()
    known = find_overlaps(recs)
    check_cooking_twins(recs)
    burn = check_fuel(tags)

    types = Counter(r.type for r in recs)
    total = sum(types.values())
    print(f"Items in the pack: {len(items)}")
    print(f"Recipes after family expansion: {total}")
    for t in TYPES:
        print(f"  {t:17} {types.get(t, 0):5}")
    print(f"  {'crafting':17} {types['shaped'] + types['shapeless']:5}")
    print(f"  {'cooking':17} {sum(types[t] for t in COOKING_TIME):5}")
    print("Per file:")
    for fn, n in sorted(loader.file_counts.items()):
        print(f"  {fn:28} {n:5}")
    print("Families (rows x templates -> recipes):")
    for name, nrows, ntemps, made in loader.family_stats:
        print(f"  {name:28} {nrows:3} x {ntemps:2} -> {made:4}")
    results = {r.result for r in recs if r.result}
    print(f"Distinct results: {len(results)}")
    unused = sorted(set(tags.raw) - tags.used)
    print(f"Item tags: {len(tags.raw)} files, {len(tags.used)} referenced"
          + (f", unreferenced: {', '.join('#' + u for u in unused)}" if unused else ""))
    by_ticks = Counter(burn.values())
    print(f"Fuel items: {len(burn)} ("
          + ", ".join(f"{n} x {t}" for t, n in sorted(by_ticks.items(), reverse=True)) + " ticks)")
    if known:
        print(f"Overlaps vanilla has too ({len(known)}):")
        for k in known:
            print(f"  {k}")
    else:
        print("Overlaps vanilla has too: none")

    if "--uncraftable" in flags:
        missing = [i for i in items if i not in results]
        print(f"Pack items no recipe produces ({len(missing)}):")
        for i in range(0, len(missing), 6):
            print("  " + "  ".join(missing[i:i + 6]))

    if warnings:
        print(f"\nWARNINGS ({len(warnings)}):")
        for w in warnings:
            print(f"  {w}")
    if problems:
        print(f"\nPROBLEMS ({len(problems)}):")
        by_kind = defaultdict(list)
        for kind, msg in problems:
            by_kind[kind].append(msg)
        for kind in sorted(by_kind):
            print(f"  [{kind}] {len(by_kind[kind])}")
            for msg in by_kind[kind]:
                print(f"    {msg}")
        print("\nFAILED")
        return 1
    print("\nOK: no problems")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
