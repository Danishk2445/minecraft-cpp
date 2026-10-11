// EntityModels.h -- vanilla layer definitions of entity models: the player,
// and the shield and trident (drawn as items). Mobs join in M12.
#pragma once

#include "entity/model/ModelPart.h"

namespace mc {

// PlayerModel.createMesh(CubeDeformation.NONE, slim) (64x64): head with hat,
// body with jacket, arms with sleeves, legs with pants. Parts sit at their
// vanilla rest pivots; PlayerPose moves them per frame.
ModelDefinition playerModel(bool slim);
// ShieldModel.createLayer (64x64): plate and handle.
ModelDefinition shieldModel();
// TridentModel.createLayer (32x32): pole, base and three spikes.
ModelDefinition tridentModel();

} // namespace mc
