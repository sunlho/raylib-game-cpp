#pragma once

namespace Runtime::Phases {

struct MovementUpdate {};
struct PrePhysics {};
struct PhysicsStep {};
struct PostPhysics {};
struct FixedGameplay {};
struct CharacterUpdate {};
struct CameraFollow {};

struct DrawBackground {};
struct DrawWorld {};
struct DrawSortedWorld {};

} // namespace Runtime::Phases
