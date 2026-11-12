#include "F4TRemotePlayerState.h"

#include <cassert>

int main()
{
	using namespace F4T::RemotePlayerState;
	RemotePlayerState input;
	input.playerId = 7;
	input.angleZ = 1.25F;
	input.isMoving = input.isSprinting = input.isSneaking = true;
	input.isJumping = input.isCrouching = input.weaponDrawn = true;
	input.movementSpeed = 240.0F;
	input.animationGraphSpeed = 130.0F;
	input.animationDirection = 45.0F;
	input.hasAnimationDirection = true;
	input.aimPitch = 20.0F;
	input.turnDelta = 0.5F;
	input.hasAimState = true;
	input.characterName = "name";
	input.equippedItems.resize(4);
	input.appearance.tints.resize(8);
	input.actionEvents.resize(5);
	input.powerArmorPieces.resize(3);
	const auto snapshot = AnimationSnapshot(input);
	assert(snapshot.playerId == input.playerId && snapshot.angleZ == input.angleZ);
	assert(snapshot.isMoving && snapshot.isSprinting && snapshot.isSneaking);
	assert(snapshot.isJumping && snapshot.isCrouching && snapshot.weaponDrawn);
	assert(snapshot.movementSpeed == input.movementSpeed && snapshot.animationGraphSpeed == input.animationGraphSpeed);
	assert(snapshot.hasAnimationDirection && snapshot.animationDirection == input.animationDirection);
	assert(snapshot.hasAimState && snapshot.aimPitch == input.aimPitch && snapshot.turnDelta == input.turnDelta);
	assert(snapshot.characterName.empty() && snapshot.equippedItems.empty());
	assert(snapshot.appearance.tints.empty() && snapshot.actionEvents.empty() && snapshot.powerArmorPieces.empty());
}
