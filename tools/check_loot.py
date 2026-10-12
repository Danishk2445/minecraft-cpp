#!/usr/bin/env python3
"""check_loot.py -- validates the block loot rules and the tool tags.

    python3 -I tools/check_loot.py [--verbose] [--explain BLOCK ...] [--pack DIR]

Checks data/loot/blocks.json and the tool tags (data/tags/blocks/mineable/*.json,
needs_{stone,iron,diamond}_tool.json, sword_efficient.json, sword_instantly_mines.json)
against the resource pack (block ids and properties from blockstates/, item ids from
items/) and the game's own data (the other block tags, data/items/items.json and the
behavior-class state schemas):

  * every id, glob and #tag in the tool tags and in the rules' "match" lists names at
    least one block; tags resolve as the game resolves them (values, then excludes,
    with nested #tags);
  * every drop item exists, and "self" is used only on blocks that have an item;
  * every "when" property and value, and every "count_from" (integer) property, exists
    on every block the rule matches;
  * no rule is fully shadowed by earlier rules (the first matching rule wins), and no
    rule lists a block whose states earlier rules already take;
  * rules and drops have the documented shape (keys, count forms, chances, xp).

Block properties are those the blockstate files mention (variant keys and multipart
"when" clauses), plus the state schemas of the game's behavior classes
(data/blocks/classes.json with the table in src/world/block/BlockRegistry.cpp), which
add the states block-entity blocks never show in their blockstate files (a bed's
"part", TNT's "unstable", a decorated pot's "cracked").

It prints how many blocks each tool tag holds and how many blocks get a loot rule, and
cross-checks data/blocks/properties.json: a block that needs a tool but is in no mineable
tag can never drop. Exit status 1 when there are errors (warnings don't fail).
"""

import argparse
import itertools
import json
import re
import sys
from collections import namedtuple
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "data"
TAG_DIR = DATA / "tags" / "blocks"
LOOT_FILE = DATA / "loot" / "blocks.json"
TOOL_TAGS = ["mineable/pickaxe", "mineable/axe", "mineable/shovel", "mineable/hoe",
             "needs_stone_tool", "needs_iron_tool", "needs_diamond_tool",
             "sword_efficient", "sword_instantly_mines"]
MINEABLE = TOOL_TAGS[:4]
NEEDS = TOOL_TAGS[4:7]
RULE_KEYS = {"match", "when", "drops", "shears", "xp", "player_only"}
DROP_KEYS = {"item", "count", "count_from", "chance", "else"}

# A checked loot rule: its position, the JSON, the blocks it matches, its "when" as
# property -> allowed values, a label for messages, and the matched blocks its "when"
# can't apply to (a missing property or value, already reported).
Rule = namedtuple("Rule", "index doc matched conds where invalid")


class Report:
    def __init__(self):
        self.errors, self.warnings = [], []

    def error(self, where, msg):
        self.errors.append(f"{where}: {msg}")

    def warn(self, where, msg):
        self.warnings.append(f"{where}: {msg}")


R = Report()


class PerBlock:
    """Collects one message for many blocks, so a rule over #candles reports once, not 17 times."""

    def __init__(self):
        self.found = {}

    def add(self, kind, where, msg, block):
        self.found.setdefault((kind, where, msg), []).append(block)

    def flush(self):
        for (kind, where, msg), bl in self.found.items():
            names = ", ".join(bl[:6]) + (f" and {len(bl) - 6} more" if len(bl) > 6 else "")
            (R.error if kind == "error" else R.warn)(where, f"{names}: {msg}")
        self.found = {}


def load_json(path):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError) as e:
        R.error(str(path), f"can't read JSON ({e})")
        return None


def short(id_):
    """'#minecraft:logs' -> 'logs', 'minecraft:stone' -> 'stone'."""
    id_ = id_[1:] if id_.startswith("#") else id_
    return id_[len("minecraft:"):] if id_.startswith("minecraft:") else id_


_glob_cache = {}


def glob_match(pattern, text):
    """The game's globMatch: '*' matches any run of characters, nothing else is special."""
    rx = _glob_cache.get(pattern)
    if rx is None:
        rx = _glob_cache[pattern] = re.compile(".*".join(re.escape(p) for p in pattern.split("*")), re.S)
    return rx.fullmatch(text) is not None


def value_key(v):
    """Sort block-state values numerically when they are numbers ("2" before "10")."""
    return (0, int(v), "") if re.fullmatch(r"-?\d+", v) else (1, 0, v)


def plural(n, word):
    return f"{n} {word}" + ("" if n == 1 else "s")


# ---------------------------------------------------------------- pack: blocks, properties, items

def find_pack(arg):
    candidates = [Path(arg)] if arg else []
    candidates.append(ROOT / "Java Default Textures 1.21.7 - 1.21.8" / "assets" / "minecraft")
    candidates += sorted(p.parent for p in ROOT.glob("*/assets/minecraft/blockstates"))
    for c in candidates:
        if (c / "blockstates").is_dir() and (c / "items").is_dir():
            return c
        if (c / "assets" / "minecraft" / "blockstates").is_dir():
            return c / "assets" / "minecraft"
    sys.exit("check_loot: can't find the resource pack (assets/minecraft with blockstates/ and items/); use --pack")


def blockstate_properties(doc):
    """Property -> values mentioned by a blockstate file's variant keys and multipart 'when' clauses."""
    props = {}

    def add(k, v):
        if isinstance(v, bool):
            v = "true" if v else "false"
        for value in str(v).split("|"):
            props.setdefault(k, set()).add(value)

    for key in (doc.get("variants") or {}):
        for part in filter(None, key.split(",")):
            k, _, v = part.partition("=")
            add(k, v)

    def walk(when):
        if isinstance(when, dict):
            for k, v in when.items():
                if k in ("OR", "AND") and isinstance(v, list):
                    for sub in v:
                        walk(sub)
                else:
                    add(k, v)

    for part in doc.get("multipart") or []:
        walk(part.get("when"))
    return props


def load_pack(pack):
    blocks = {}
    for f in sorted((pack / "blockstates").glob("*.json")):
        doc = load_json(f)
        blocks[f.stem] = blockstate_properties(doc) if isinstance(doc, dict) else {}
    items = {f.stem for f in (pack / "items").glob("*.json")}
    return blocks, items


# ---------------------------------------------------------------- the game's behavior-class schemas

# Used only when the class schemas can't be read: the states of block-entity blocks whose
# blockstate files don't show them (vanilla BedBlock, TntBlock, DecoratedPotBlock).
FALLBACK_CODE_PROPERTIES = {
    "*_bed": {"part": {"head", "foot"}, "facing": {"north", "south", "west", "east"}, "occupied": {"false", "true"}},
    "tnt": {"unstable": {"false", "true"}},
    "decorated_pot": {"cracked": {"false", "true"}, "facing": {"north", "south", "west", "east"},
                      "waterlogged": {"false", "true"}},
}


def parse_schema_spec(spec, params):
    """The compact schema form of BlockRegistry.cpp: name:v1,v2[=d] | name:bool | name:LO-HI | name:h | name:6."""
    for k, v in (params or {}).items():
        spec = spec.replace("$" + k, v if isinstance(v, str) else json.dumps(v))
    props = {}
    for token in spec.split():
        name, _, body = token.partition(":")
        body = body.split("=")[0]
        span = re.fullmatch(r"(-?\d+)-(-?\d+)", body)
        if body == "bool":
            values = ["false", "true"]
        elif body == "h":
            values = ["north", "south", "west", "east"]
        elif body == "6":
            values = ["down", "up", "north", "south", "west", "east"]
        elif span:
            values = [str(v) for v in range(int(span[1]), int(span[2]) + 1)]
        else:
            values = [v for v in body.split(",") if v]
        props[name] = set(values)
    return props


def load_engine_schemas(block_ids):
    """Block -> (class, properties) for blocks with a behavior class; None if the sources are missing."""
    classes_path = DATA / "blocks" / "classes.json"
    cpp_path = ROOT / "src" / "world" / "block" / "BlockRegistry.cpp"
    if not classes_path.is_file() or not cpp_path.is_file():
        return None, "no data/blocks/classes.json or src/world/block/BlockRegistry.cpp"
    text = cpp_path.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"classSchemas\(\)\s*\{(.*?)\n\s*\};", text, re.S)
    table = {}
    if m:
        for em in re.finditer(r'\{\s*"([A-Za-z0-9_]+)"\s*,\s*((?:"[^"]*"\s*)+)\}', m.group(1)):
            table[em.group(1)] = "".join(re.findall(r'"([^"]*)"', em.group(2)))
    if not table:
        return None, "couldn't parse the classSchemas() table in BlockRegistry.cpp"
    rules = load_json(classes_path)
    if not isinstance(rules, list):
        return None, "data/blocks/classes.json is not a list"
    out = {}
    for b in block_ids:
        for r in rules:
            if not isinstance(r, dict) or "match" not in r:
                continue
            pats = r["match"] if isinstance(r["match"], list) else [r["match"]]
            if any(glob_match(short(p), b) for p in pats):
                cls = r.get("class", "simple")
                if cls in table:
                    out[b] = (cls, parse_schema_spec(table[cls], r.get("params")))
                break
    return out, f"{len(table)} class schemas"


# ---------------------------------------------------------------- tags

class Tags:
    """data/tags/blocks/**.json resolved like core/TagSet.cpp: values in order, then excludes."""

    def __init__(self, tag_dir, block_ids):
        self.block_ids = block_ids
        self.raw = {}
        for path in sorted(tag_dir.rglob("*.json")):
            name = path.relative_to(tag_dir).with_suffix("").as_posix()
            doc = load_json(path)
            if not isinstance(doc, dict):
                continue
            vals = [v for v in doc.get("values", []) if isinstance(v, str)]
            excl = [v for v in doc.get("exclude", []) if isinstance(v, str)]
            self.raw[name] = (vals, excl)
        self.sets, self.state = {}, {}
        self.problems = []  # (tag, message)
        self.entry_effect = {}  # (tag, "values" | "exclude", index) -> blocks the entry added or removed

    def entry(self, entry):
        """Blocks an entry names, or None if it names nothing known."""
        name = short(entry)
        if entry.startswith("#"):
            if name not in self.raw:
                return None
            return set(self.resolve(name))
        if "*" in name:
            hits = {b for b in self.block_ids if glob_match(name, b)}
            return hits or None
        return {name} if name in self.block_ids else None

    def resolve(self, name):
        if self.state.get(name) == 2:
            return self.sets[name]
        if self.state.get(name) == 1:
            self.problems.append((name, "references itself"))
            return set()
        self.state[name] = 1
        vals, excl = self.raw.get(name, ([], []))
        out = set()
        for i, v in enumerate(vals):
            got = self.entry(v)
            if got is None:
                self.problems.append((name, f"value {v!r} names no block or tag"))
                got = set()
            elif not got:
                self.problems.append((name, f"value {v!r} is an empty tag"))
            self.entry_effect[(name, "values", i)] = got - out
            out |= got
        for i, v in enumerate(excl):
            got = self.entry(v)
            if got is None:
                self.problems.append((name, f"exclude {v!r} names no block or tag"))
                got = set()
            self.entry_effect[(name, "exclude", i)] = got & out
            out -= got
        self.sets[name], self.state[name] = out, 2
        return out


# ---------------------------------------------------------------- loot

def count_ok(count, where):
    if isinstance(count, bool):
        R.error(where, f"count {count!r} is not a number")
    elif isinstance(count, int):
        if count < 1:
            R.error(where, f"count {count} is less than 1")
    elif isinstance(count, list):
        if len(count) != 2 or not all(isinstance(c, int) and not isinstance(c, bool) for c in count):
            R.error(where, f"count {count!r} should be [min, max] integers")
        elif not 0 <= count[0] <= count[1] or count[1] < 1:
            R.error(where, f"count range {count!r} needs 0 <= min <= max and max >= 1")
    elif isinstance(count, dict):
        extra = {k for k in count if not k.startswith("_")} - {"binomial", "plus"}
        if extra:
            R.error(where, f"count has unknown keys {sorted(extra)}")
        b = count.get("binomial")
        if (not isinstance(b, list) or len(b) != 2 or not isinstance(b[0], int) or isinstance(b[0], bool)
                or not isinstance(b[1], (int, float)) or isinstance(b[1], bool)):
            R.error(where, f"binomial should be [n, p], got {b!r}")
        elif b[0] < 1 or not 0 < b[1] < 1:
            R.error(where, f"binomial {b!r} needs n >= 1 and 0 < p < 1")
        plus = count.get("plus", 0)
        if not isinstance(plus, int) or isinstance(plus, bool) or plus < 0:
            R.error(where, f"plus {plus!r} should be an integer >= 0")
    else:
        R.error(where, f"count {count!r} should be an integer, [min, max] or a binomial")


def own_item(block, items, block_items, not_block_items):
    """(item, how) the block drops by default: its namesake item, else the item that places it."""
    if block in items and block not in not_block_items:
        return block, "namesake"
    for it, bl in block_items.items():
        if bl == block and it in items:
            return it, "block_items"
    return None, None


def describe_drop(d, own):
    name = own if d.get("item") == "self" else d.get("item")
    c = d.get("count", 1)
    if "count_from" in d:
        c = f"<{d['count_from']}>"
    elif isinstance(c, list):
        c = f"{c[0]}-{c[1]}"
    elif isinstance(c, dict):
        b = c.get("binomial", ["?", "?"])
        c = f"B({b[0]},{b[1]})" + (f"+{c['plus']}" if c.get("plus") else "")
    s = f"{c}x {name}"
    return s + (f" @{d['chance']}" if "chance" in d else "")


def check_loot(blocks, props_of, engine, items, block_items, not_block_items, tags):
    doc = load_json(LOOT_FILE)
    if doc is None:
        return [], {}
    if not isinstance(doc, dict) or not isinstance(doc.get("rules"), list):
        R.error("loot", 'top level should be {"rules": [...]}')
        return [], {}
    extra = {k for k in doc if not k.startswith("_")} - {"rules"}
    if extra:
        R.error("loot", f"unknown top-level keys {sorted(extra)}")

    rules = []
    for i, r in enumerate(doc["rules"]):
        where = f"loot rule {i}"
        if not isinstance(r, dict):
            R.error(where, "not an object")
            continue
        extra = {k for k in r if not k.startswith("_")} - RULE_KEYS
        if extra:
            R.error(where, f"unknown keys {sorted(extra)}")
        match = r.get("match")
        if isinstance(match, str):
            match = [match]
        if not isinstance(match, list) or not match or not all(isinstance(m, str) for m in match):
            R.error(where, '"match" should be a non-empty list of ids, globs and #tags')
            continue
        matched = set()
        for m in match:
            got = tags.entry(m)
            if not got:
                R.error(where, f"match entry {m!r} names no block" + (" (unknown tag)" if m.startswith("#") else ""))
                continue
            dup = got & matched
            if dup == got:
                R.warn(where, f"match entry {m!r} adds no block the rule doesn't already list")
            matched |= got
        where = f"loot rule {i} ({', '.join(match[:3])}{', ...' if len(match) > 3 else ''})"

        when = r.get("when", {})
        if not isinstance(when, dict):
            R.error(where, '"when" should be an object')
            when = {}
        conds = {}
        for p, v in when.items():
            if p.startswith("_"):
                continue
            vals = v if isinstance(v, list) else [v]
            if not vals or not all(isinstance(x, str) for x in vals):
                R.error(where, f'"when" {p}: values should be strings (or a non-empty list of them)')
                continue
            conds[p] = set(vals)
        agg = PerBlock()
        invalid = set()  # blocks the "when" can't apply to (already reported)
        for b in sorted(matched):
            bp = props_of[b]
            for p, vals in conds.items():
                if p not in bp:
                    agg.add("error", where, f"no property {p!r} (for \"when\")", b)
                    invalid.add(b)
                    continue
                bad = vals - bp[p]
                if bad:
                    agg.add("error", where, f"{p} has no value {'|'.join(sorted(bad))} "
                                            f"(values: {'|'.join(sorted(bp[p], key=value_key))})", b)
                    invalid.add(b)
                elif engine is not None and b in engine:
                    cls, eprops = engine[b]
                    if p not in eprops or (vals - eprops[p]):
                        agg.add("warn", where, f"the game's '{cls}' state schema lacks {p}={'|'.join(sorted(vals))}, "
                                               f"so the rule can't match there at run time", b)

        for key in ("drops", "shears"):
            if key not in r:
                if key == "drops":
                    R.error(where, '"drops" is required ([] for nothing)')
                continue
            lst = r[key]
            if not isinstance(lst, list):
                R.error(where, f'"{key}" should be a list')
                continue
            for j, d in enumerate(lst):
                dw = f"{where} {key}[{j}]"
                if not isinstance(d, dict):
                    R.error(dw, "not an object")
                    continue
                extra = {k for k in d if not k.startswith("_")} - DROP_KEYS
                if extra:
                    R.error(dw, f"unknown keys {sorted(extra)}")
                it = d.get("item")
                if not isinstance(it, str):
                    R.error(dw, '"item" is required')
                elif it == "self":
                    for b in sorted(matched):
                        if own_item(b, items, block_items, not_block_items)[0] is None:
                            agg.add("error", dw, '"self", but the block has no item', b)
                elif it not in items:
                    R.error(dw, f"unknown item {it!r}")
                el = d.get("else")
                if el is not None:
                    if not isinstance(el, dict) or not isinstance(el.get("item"), str):
                        R.error(dw, '"else" should be a drop with an "item"')
                    elif el["item"] != "self" and el["item"] not in items:
                        R.error(dw, f"unknown item {el['item']!r} in else")
                if "count" in d and "count_from" in d:
                    R.error(dw, '"count" and "count_from" exclude each other')
                if "count" in d:
                    count_ok(d["count"], dw)
                if "count_from" in d:
                    cf = d["count_from"]
                    if not isinstance(cf, str):
                        R.error(dw, '"count_from" should be a property name')
                    else:
                        for b in sorted(matched):
                            vals = props_of[b].get(cf)
                            if vals is None:
                                agg.add("error", dw, f"no property {cf!r} (for count_from)", b)
                            elif not all(re.fullmatch(r"-?\d+", v) for v in vals):
                                agg.add("error", dw, f"count_from {cf} is not an integer property", b)
                            elif min(int(v) for v in vals) < 1:
                                agg.add("warn", dw, f"count_from {cf} can be {min(int(v) for v in vals)}", b)
                if "chance" in d:
                    c = d["chance"]
                    if not isinstance(c, (int, float)) or isinstance(c, bool) or not 0 < c <= 1:
                        R.error(dw, f"chance {c!r} should be a number in (0, 1]")
                    elif c == 1:
                        R.warn(dw, "chance 1 is the default")
        if "xp" in r:
            xp = r["xp"]
            if (not isinstance(xp, list) or len(xp) != 2
                    or not all(isinstance(x, int) and not isinstance(x, bool) for x in xp)
                    or not 0 <= xp[0] <= xp[1] or xp[1] == 0):
                R.error(where, f"xp {xp!r} should be [min, max] integers with 0 <= min <= max, max > 0")
        agg.flush()
        rules.append(Rule(i, r, matched, conds, where, invalid))

    # First match wins: walk each block's states (over the properties its rules test) in rule order.
    tested = {}
    for rule in rules:
        for b in rule.matched - rule.invalid:
            tested.setdefault(b, set()).update(rule.conds)
    space = {}
    for b, ps in tested.items():
        ps = sorted(ps)
        space[b] = (ps, list(itertools.product(*(sorted(props_of[b][p]) for p in ps))))
    taken = {b: set() for b in tested}
    decides = {}  # block -> [(rule, how many of its states the rule decides, how many states it has)]
    for rule in rules:
        i, conds, where = rule.index, rule.conds, rule.where
        live = sorted(rule.matched - rule.invalid)
        dead = []
        for b in live:
            ps, states = space[b]
            hit = {s for s in states if all(s[ps.index(p)] in vals for p, vals in conds.items())}
            new = hit - taken[b]
            taken[b] |= hit
            if new:
                decides.setdefault(b, []).append((rule, len(new), len(states)))
            else:
                dead.append(b)
        if not live:
            continue  # nothing it can match; the reasons are reported above
        if len(dead) == len(live):
            R.error(where, "fully shadowed: earlier rules already decide every state it matches")
        elif dead:
            R.warn(where, f"earlier rules already decide every matched state of {', '.join(dead)}")
    return rules, decides


# ---------------------------------------------------------------- properties.json cross-checks

def requires_tool(block_ids):
    path = DATA / "blocks" / "properties.json"
    doc = load_json(path) if path.is_file() else None
    if not isinstance(doc, list):
        return None
    out = {}
    for b in block_ids:
        for r in doc:
            if isinstance(r, dict) and "requires_tool" in r:
                pats = r.get("match", [])
                pats = pats if isinstance(pats, list) else [pats]
                if any(glob_match(short(p), b) for p in pats):
                    out[b] = bool(r["requires_tool"])
                    break
    return out


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--pack", help="the resource pack's assets/minecraft directory")
    ap.add_argument("--verbose", "-v", action="store_true", help="list each tool tag's blocks and more")
    ap.add_argument("--explain", nargs="+", metavar="BLOCK", help="show the rules that decide a block's drops")
    args = ap.parse_args()

    pack = find_pack(args.pack)
    blocks, items = load_pack(pack)
    block_ids = set(blocks)
    engine, engine_note = load_engine_schemas(block_ids)
    props_of = {}
    for b in blocks:
        p = {k: set(v) for k, v in blocks[b].items()}
        if engine and b in engine:
            extra = engine[b][1]
        elif engine is None:
            extra = next((v for g, v in FALLBACK_CODE_PROPERTIES.items() if glob_match(g, b)), {})
        else:
            extra = {}
        for k, v in extra.items():
            p.setdefault(k, set()).update(v)
        props_of[b] = p

    items_doc = load_json(DATA / "items" / "items.json") or {}
    block_items = {k: v for k, v in items_doc.get("block_items", {}).items() if not k.startswith("_")}
    not_block_items = set(items_doc.get("not_block_items", []))
    for it, bl in block_items.items():
        if it not in items:
            R.warn("data/items/items.json", f"block_items: unknown item {it!r}")
        if bl not in block_ids:
            R.warn("data/items/items.json", f"block_items: unknown block {bl!r}")

    # Tags: resolve every file (the tool tags may reference any of them).
    tags = Tags(TAG_DIR, block_ids)
    for name in tags.raw:
        tags.resolve(name)
    for name, msg in tags.problems:
        (R.error if name in TOOL_TAGS else R.warn)(f"tag {name}", msg)
    for name in TOOL_TAGS:
        if name not in tags.raw:
            R.error(f"tag {name}", f"missing file {TAG_DIR / (name + '.json')}")
            continue
        vals, excl = tags.raw[name]
        for kind, lst in (("values", vals), ("exclude", excl)):
            for idx, v in enumerate(lst):
                eff = tags.entry_effect.get((name, kind, idx), set())
                if kind == "values" and not eff and tags.entry(v):
                    R.warn(f"tag {name}", f"value {v!r} adds nothing earlier values don't")
                if kind == "exclude" and not eff and tags.entry(v):
                    R.warn(f"tag {name}", f"exclude {v!r} removes nothing")

    rules, decides = check_loot(blocks, props_of, engine, items, block_items, not_block_items, tags)

    # Tool-tag consistency.
    sets = {n: tags.sets.get(n, set()) for n in TOOL_TAGS}
    for b in sorted(block_ids):
        inm = [n for n in MINEABLE if b in sets[n]]
        if len(inm) > 1:
            R.warn("tool tags", f"{b} is in {' and '.join(inm)}")
        inn = [n for n in NEEDS if b in sets[n]]
        if len(inn) > 1:
            R.warn("tool tags", f"{b} is in {' and '.join(inn)}")
        if inn and not inm:
            R.warn("tool tags", f"{b} is in {inn[0]} but in no mineable tag")
    rt = requires_tool(block_ids)
    if rt is not None:
        never = sorted(b for b, need in rt.items() if need and b != "cobweb" and not any(b in sets[n] for n in MINEABLE))
        for b in never:
            R.warn("data/blocks/properties.json",
                   f"{b} has requires_tool but is in no mineable tag, so it can never drop")

    # Default drops that come from block_items rather than a namesake item are ambiguous: flag them.
    ruled = set(decides)
    for b in sorted(block_ids - ruled):
        it, how = own_item(b, items, block_items, not_block_items)
        if how == "block_items":
            R.warn("loot", f"{b} matches no rule; its default drop would be {it} (via block_items)")

    # ------------------------------------------------------------ report
    print(f"check_loot: {plural(len(block_ids), 'block')} and {plural(len(items), 'item')} "
          f"from {pack.relative_to(ROOT) if pack.is_relative_to(ROOT) else pack}")
    if engine is None:
        print(f"  block states from blockstate files and a built-in list for beds, TNT and decorated pots "
              f"({engine_note})")
    else:
        added = sorted({f"{b}.{p}" for b, (_, ep) in engine.items() for p in ep if p not in blocks[b]})
        print(f"  block states from blockstate files plus the game's {engine_note} "
              f"({len(engine)} blocks; {len(added)} block properties only the schemas have)")
    print("\nTool tags (blocks):")
    for n in TOOL_TAGS:
        print(f"  {n:24} {len(sets[n]):4}")
        if args.verbose:
            print("    " + " ".join(sorted(sets[n])))
    anym = set().union(*(sets[n] for n in MINEABLE))
    print(f"  {'(any mineable tag)':24} {len(anym):4}")

    nrules = len(rules)
    nwhen = sum(1 for rule in rules if rule.conds)
    covered = len(ruled)
    defaults = sorted(block_ids - ruled)
    no_item = [b for b in defaults if own_item(b, items, block_items, not_block_items)[0] is None]
    print(f"\nLoot ({LOOT_FILE.relative_to(ROOT)}): {plural(nrules, 'rule')} ({nwhen} with state conditions, "
          f"{sum(1 for rule in rules if 'shears' in rule.doc)} with shears drops, "
          f"{sum(1 for rule in rules if 'xp' in rule.doc)} with xp)")
    print(f"  {covered} blocks get a non-default loot rule; the other {len(defaults)} drop their own item "
          f"({len(no_item)} of them have none and drop nothing)")
    if args.verbose:
        print("  no rule and no item: " + " ".join(no_item))

    if args.explain:
        for b in args.explain:
            b = short(b)
            print(f"\n{b}:")
            if b not in block_ids:
                print("  not a block")
                continue
            own = own_item(b, items, block_items, not_block_items)[0]
            print(f"  properties: " + (", ".join(f"{p}={'|'.join(sorted(v, key=value_key))}"
                                                 for p, v in sorted(props_of[b].items()))
                                     or "none"))
            print(f"  tags: " + (", ".join(n for n in sorted(tags.sets) if b in tags.sets[n]) or "none"))
            hits = decides.get(b, [])
            default = f"no rule, drops {'1x ' + own if own else 'nothing'}"
            for rule, n, total in hits:
                r = rule.doc
                cond = ", ".join(f"{p}={'|'.join(sorted(v, key=value_key))}" for p, v in rule.conds.items())
                drops = ", ".join(describe_drop(d, own) for d in r.get("drops", [])) or "nothing"
                if "shears" in r:
                    drops += "; shears: " + (", ".join(describe_drop(d, own) for d in r["shears"]) or "nothing")
                if "xp" in r:
                    drops += f"; xp {r['xp'][0]}-{r['xp'][1]}"
                print(f"  rule {rule.index}" + (f" [{cond}]" if cond else "") + f" ({n}/{total} states): {drops}")
            if not hits:
                print(f"  {default}")
            elif sum(n for _, n, _ in hits) < hits[0][2]:
                print(f"  other states: {default}")

    print()
    for w in R.warnings:
        print("warning: " + w)
    for e in R.errors:
        print("error: " + e)
    print(f"{plural(len(R.errors), 'problem')}, {plural(len(R.warnings), 'warning')}")
    return 1 if R.errors else 0



if __name__ == "__main__":
    sys.exit(main())
