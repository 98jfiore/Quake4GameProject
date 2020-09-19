#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "../Weapon.h"
#include "../ai/AI.h"
#include "../ai/AI_Manager.h"
#include "../client/ClientEffect.h"

#ifndef __GAME_PROJECTILE_H__
#include "../Projectile.h"
#endif

class rvWeaponRocketLauncher : public rvWeapon {
public:

	CLASS_PROTOTYPE( rvWeaponRocketLauncher );

	rvWeaponRocketLauncher ( void );
	~rvWeaponRocketLauncher ( void );

	virtual void			Spawn				( void );
	virtual void			Think				( void );

	void					Save( idSaveGame *saveFile ) const;
	void					Restore( idRestoreGame *saveFile );
	void					PreSave				( void );
	void					PostSave			( void );
	void					Attack				(bool altAttack, int num_attacks, float spread, float fuseOffset, float power);
	void					LaunchProjectiles	(idDict& dict, const idVec3& muzzleOrigin, const idMat3& muzzleAxis, int num_projectiles, float spread, float fuseOffset, float power);


#ifdef _XENON
	virtual bool		AllowAutoAim			( void ) const { return false; }
#endif

protected:

	virtual void			OnLaunchProjectile	( idProjectile* proj );

	void					SetRocketState		( const char* state, int blendFrames );

	rvClientEntityPtr<rvClientEffect>	guideEffect;
	idList< idEntityPtr<idEntity> >		guideEnts;
	float								guideSpeedSlow;
	float								guideSpeedFast;
	float								guideRange;
	float								guideAccelTime;

	rvStateThread						rocketThread;

	float								reloadRate;

	bool								idleEmpty;

private:

	stateResult_t		State_Idle				( const stateParms_t& parms );
	stateResult_t		State_Fire				( const stateParms_t& parms );
	stateResult_t		State_Raise				( const stateParms_t& parms );
	stateResult_t		State_Lower				( const stateParms_t& parms );
	
	stateResult_t		State_Rocket_Idle		( const stateParms_t& parms );
	stateResult_t		State_Rocket_Reload		( const stateParms_t& parms );
	
	stateResult_t		Frame_AddToClip			( const stateParms_t& parms );
	
	CLASS_STATES_PROTOTYPE ( rvWeaponRocketLauncher );
};

CLASS_DECLARATION( rvWeapon, rvWeaponRocketLauncher )
END_CLASS

/*
================
rvWeaponRocketLauncher::rvWeaponRocketLauncher
================
*/
rvWeaponRocketLauncher::rvWeaponRocketLauncher ( void ) {
}

/*
================
rvWeaponRocketLauncher::~rvWeaponRocketLauncher
================
*/
rvWeaponRocketLauncher::~rvWeaponRocketLauncher ( void ) {
	if ( guideEffect ) {
		guideEffect->Stop();
	}
}

/*
================
rvWeaponRocketLauncher::Spawn
================
*/
void rvWeaponRocketLauncher::Spawn ( void ) {
	float f;

	idleEmpty = false;
	
	spawnArgs.GetFloat ( "lockRange", "0", guideRange );

	spawnArgs.GetFloat ( "lockSlowdown", ".25", f );
	attackDict.GetFloat ( "speed", "0", guideSpeedFast );
	guideSpeedSlow = guideSpeedFast * f;
	
	reloadRate = SEC2MS ( spawnArgs.GetFloat ( "reloadRate", ".8" ) );
	
	guideAccelTime = SEC2MS ( spawnArgs.GetFloat ( "lockAccelTime", ".25" ) );
	
	// Start rocket thread
	rocketThread.SetName ( viewModel->GetName ( ) );
	rocketThread.SetOwner ( this );

	// Adjust reload animations to match the fire rate
	idAnim* anim;
	int		animNum;
	float	rate;
	animNum = viewModel->GetAnimator()->GetAnim ( "reload" );
	if ( animNum ) {
		anim = (idAnim*)viewModel->GetAnimator()->GetAnim ( animNum );
		rate = (float)anim->Length() / (float)SEC2MS(spawnArgs.GetFloat ( "reloadRate", ".8" ));
		anim->SetPlaybackRate ( rate );
	}

	animNum = viewModel->GetAnimator()->GetAnim ( "reload_empty" );
	if ( animNum ) {
		anim = (idAnim*)viewModel->GetAnimator()->GetAnim ( animNum );
		rate = (float)anim->Length() / (float)SEC2MS(spawnArgs.GetFloat ( "reloadRate", ".8" ));
		anim->SetPlaybackRate ( rate );
	}

	SetState ( "Raise", 0 );	
	SetRocketState ( "Rocket_Idle", 0 );
}

/*
================
rvWeaponRocketLauncher::Think
================
*/
void rvWeaponRocketLauncher::Think ( void ) {	
	trace_t	tr;
	int		i;

	rocketThread.Execute ( );

	// Let the real weapon think first
	rvWeapon::Think ( );

	// IF no guide range is set then we dont have the mod yet	
	if ( !guideRange ) {
		return;
	}
	
	if ( !wsfl.zoom ) {
		if ( guideEffect ) {
			guideEffect->Stop();
			guideEffect = NULL;
		}

		for ( i = guideEnts.Num() - 1; i >= 0; i -- ) {
			idGuidedProjectile* proj = static_cast<idGuidedProjectile*>(guideEnts[i].GetEntity());
			if ( !proj || proj->IsHidden ( ) ) {
				guideEnts.RemoveIndex ( i );
				continue;
			}
			
			// If the rocket is still guiding then stop the guide and slow it down
			if ( proj->GetGuideType ( ) != idGuidedProjectile::GUIDE_NONE ) {
				proj->CancelGuide ( );				
				proj->SetSpeed ( guideSpeedFast, (1.0f - (proj->GetSpeed ( ) - guideSpeedSlow) / (guideSpeedFast - guideSpeedSlow)) * guideAccelTime );
			}
		}

		return;
	}
						
	// Cast a ray out to the lock range
// RAVEN BEGIN
// ddynerman: multiple clip worlds
	gameLocal.TracePoint(	owner, tr, 
							playerViewOrigin, 
							playerViewOrigin + playerViewAxis[0] * guideRange, 
							MASK_SHOT_RENDERMODEL, owner );
// RAVEN END
	
	for ( i = guideEnts.Num() - 1; i >= 0; i -- ) {
		idGuidedProjectile* proj = static_cast<idGuidedProjectile*>(guideEnts[i].GetEntity());
		if ( !proj || proj->IsHidden() ) {
			guideEnts.RemoveIndex ( i );
			continue;
		}
		
		// If the rocket isnt guiding yet then adjust its speed back to normal
		if ( proj->GetGuideType ( ) == idGuidedProjectile::GUIDE_NONE ) {
			proj->SetSpeed ( guideSpeedSlow, (proj->GetSpeed ( ) - guideSpeedSlow) / (guideSpeedFast - guideSpeedSlow) * guideAccelTime );
		}
		proj->GuideTo ( tr.endpos );				
	}
	
	if ( !guideEffect ) {
		guideEffect = gameLocal.PlayEffect ( gameLocal.GetEffect ( spawnArgs, "fx_guide" ), tr.endpos, tr.c.normal.ToMat3(), true, vec3_origin, true );
	} else {
		guideEffect->SetOrigin ( tr.endpos );
		guideEffect->SetAxis ( tr.c.normal.ToMat3() );
	}
}

/*
================
rvWeaponRocketLauncher::OnLaunchProjectile
================
*/
void rvWeaponRocketLauncher::OnLaunchProjectile ( idProjectile* proj ) {
	rvWeapon::OnLaunchProjectile(proj);

	// Double check that its actually a guided projectile
	if ( !proj || !proj->IsType ( idGuidedProjectile::GetClassType() ) ) {
		return;
	}

	// Launch the projectile
	idEntityPtr<idEntity> ptr;
	ptr = proj;
	guideEnts.Append ( ptr );	
}

/*
================
rvWeaponRocketLauncher::SetRocketState
================
*/
void rvWeaponRocketLauncher::SetRocketState ( const char* state, int blendFrames ) {
	rocketThread.SetState ( state, blendFrames );
}

/*
=====================
rvWeaponRocketLauncher::Save
=====================
*/
void rvWeaponRocketLauncher::Save( idSaveGame *saveFile ) const {
	saveFile->WriteObject( guideEffect );

	idEntity* ent = NULL;
	saveFile->WriteInt( guideEnts.Num() ); 
	for( int ix = 0; ix < guideEnts.Num(); ++ix ) {
		ent = guideEnts[ ix ].GetEntity();
		if( ent ) {
			saveFile->WriteObject( ent );
		}
	}
	
	saveFile->WriteFloat( guideSpeedSlow );
	saveFile->WriteFloat( guideSpeedFast );
	saveFile->WriteFloat( guideRange );
	saveFile->WriteFloat( guideAccelTime );
	
	saveFile->WriteFloat ( reloadRate );
	
	rocketThread.Save( saveFile );
}

/*
=====================
rvWeaponRocketLauncher::Restore
=====================
*/
void rvWeaponRocketLauncher::Restore( idRestoreGame *saveFile ) {
	int numEnts = 0;
	idEntity* ent = NULL;
	rvClientEffect* clientEffect = NULL;

	saveFile->ReadObject( reinterpret_cast<idClass *&>(clientEffect) );
	guideEffect = clientEffect;
	
	saveFile->ReadInt( numEnts );
	guideEnts.Clear();
	guideEnts.SetNum( numEnts );
	for( int ix = 0; ix < numEnts; ++ix ) {
		saveFile->ReadObject( reinterpret_cast<idClass *&>(ent) );
		guideEnts[ ix ] = ent;
	}
	
	saveFile->ReadFloat( guideSpeedSlow );
	saveFile->ReadFloat( guideSpeedFast );
	saveFile->ReadFloat( guideRange );
	saveFile->ReadFloat( guideAccelTime );
	
	saveFile->ReadFloat ( reloadRate );
	
	rocketThread.Restore( saveFile, this );	
}

/*
================
rvWeaponRocketLauncher::PreSave
================
*/
void rvWeaponRocketLauncher::PreSave ( void ) {
}

/*
================
rvWeaponRocketLauncher::PostSave
================
*/
void rvWeaponRocketLauncher::PostSave ( void ) {
}


/*
================
rvWeaponRocketLauncher::Attack
================
*/
void rvWeaponRocketLauncher::Attack(bool altAttack, int num_attacks, float spread, float fuseOffset, float power) {
	idVec3 muzzleOrigin;
	idMat3 muzzleAxis;

	if (!viewModel) {
		common->Warning("NULL viewmodel %s\n", __FUNCTION__);
		return;
	}

	if (viewModel->IsHidden()) {
		return;
	}

	// avoid all ammo considerations on an MP client
	if (!gameLocal.isClient) {
		//Don't remove ammo from clip
		/*
		// check if we're out of ammo or the clip is empty
		int ammoAvail = owner->inventory.HasAmmo(ammoType, ammoRequired);
		if (!ammoAvail || ((clipSize != 0) && (ammoClip <= 0))) {
			return;
		}

		owner->inventory.UseAmmo(ammoType, ammoRequired);
		if (clipSize && ammoRequired) {
			clipPredictTime = gameLocal.time;	// mp client: we predict this. mark time so we're not confused by snapshots
			ammoClip -= 1;
		}*/

		// wake up nearby monsters
		if (!wfl.silent_fire) {
			gameLocal.AlertAI(owner);
		}
	}

	// set the shader parm to the time of last projectile firing,
	// which the gun material shaders can reference for single shot barrel glows, etc
	viewModel->SetShaderParm(SHADERPARM_DIVERSITY, gameLocal.random.CRandomFloat());
	viewModel->SetShaderParm(SHADERPARM_TIMEOFFSET, -MS2SEC(gameLocal.realClientTime));

	if (worldModel.GetEntity()) {
		worldModel->SetShaderParm(SHADERPARM_DIVERSITY, viewModel->GetRenderEntity()->shaderParms[SHADERPARM_DIVERSITY]);
		worldModel->SetShaderParm(SHADERPARM_TIMEOFFSET, viewModel->GetRenderEntity()->shaderParms[SHADERPARM_TIMEOFFSET]);
	}

	// calculate the muzzle position
	if (barrelJointView != INVALID_JOINT && spawnArgs.GetBool("launchFromBarrel")) {
		// there is an explicit joint for the muzzle
		GetGlobalJointTransform(true, barrelJointView, muzzleOrigin, muzzleAxis);
	}
	else {
		// go straight out of the view
		muzzleOrigin = playerViewOrigin;
		muzzleAxis = playerViewAxis;
		muzzleOrigin += playerViewAxis[0] * muzzleOffset;
	}

	// add some to the kick time, incrementally moving repeat firing weapons back
	if (kick_endtime < gameLocal.realClientTime) {
		kick_endtime = gameLocal.realClientTime;
	}
	kick_endtime += muzzle_kick_time;
	if (kick_endtime > gameLocal.realClientTime + muzzle_kick_maxtime) {
		kick_endtime = gameLocal.realClientTime + muzzle_kick_maxtime;
	}

	// add the muzzleflash
	MuzzleFlash();

	// quad damage overlays a sound
	if (owner->PowerUpActive(POWERUP_QUADDAMAGE)) {
		viewModel->StartSound("snd_quaddamage", SND_CHANNEL_VOICE, 0, false, NULL);
	}

	// Muzzle flash effect
	bool muzzleTint = spawnArgs.GetBool("muzzleTint");
	viewModel->PlayEffect("fx_muzzleflash", flashJointView, false, vec3_origin, false, EC_IGNORE, muzzleTint ? owner->GetHitscanTint() : vec4_one);

	if (worldModel && flashJointWorld != INVALID_JOINT) {
		worldModel->PlayEffect(gameLocal.GetEffect(weaponDef->dict, "fx_muzzleflash_world"), flashJointWorld, vec3_origin, mat3_identity, false, vec3_origin, false, EC_IGNORE, muzzleTint ? owner->GetHitscanTint() : vec4_one);
	}

	owner->WeaponFireFeedback(&weaponDef->dict);

	// Inform the gui of the ammo change
	viewModel->PostGUIEvent("weapon_ammo");
	if (ammoClip == 0 && AmmoAvailable() == 0) {
		viewModel->PostGUIEvent("weapon_noammo");
	}

	// The attack is a launched projectile, do that now.
	if (!gameLocal.isClient) {
		idDict& dict = altAttack ? attackAltDict : attackDict;
		LaunchProjectiles(dict, muzzleOrigin, muzzleAxis, num_attacks, spread, fuseOffset, power);
		//asalmon:  changed to keep stats even in single player 
		statManager->WeaponFired(owner, weaponIndex, num_attacks);
	}
}

/*
================
rvWeapon::LaunchProjectiles
================
*/
void rvWeaponRocketLauncher::LaunchProjectiles(idDict& dict, const idVec3& muzzleOrigin, const idMat3& muzzleAxis, int num_projectiles, float spread, float fuseOffset, float power) {
	idProjectile*	proj;
	idEntity*		ent;
	int				i;
	float			spreadRad;
	idVec3			dir;
	idBounds		ownerBounds;

	if (gameLocal.isClient) {
		return;
	}

	// Let the AI know about the new attack
	if (!gameLocal.isMultiplayer) {
		aiManager.ReactToPlayerAttack(owner, muzzleOrigin, muzzleAxis[0]);
	}

	ownerBounds = owner->GetPhysics()->GetAbsBounds();
	spreadRad = DEG2RAD(spread);

	idVec3 dirOffset;
	idVec3 startOffset;

	spawnArgs.GetVector("dirOffset", "0 0 0", dirOffset);
	spawnArgs.GetVector("startOffset", "0 0 0", startOffset);

	for (i = 0; i < num_projectiles; i++) {
		float	 ang;
		float	 spin;
		idVec3	 dir;
		idBounds projBounds;
		idVec3	 muzzle_pos;

		// Calculate a random launch direction based on the spread
		ang = idMath::Sin(spreadRad * gameLocal.random.RandomFloat());
		spin = (float)DEG2RAD(360.0f) * gameLocal.random.RandomFloat();
		//RAVEN BEGIN
		//asalmon: xbox must use muzzle Axis for aim assistance
#ifdef _XBOX
		dir = muzzleAxis[0] + muzzleAxis[2] * (ang * idMath::Sin(spin)) - muzzleAxis[1] * (ang * idMath::Cos(spin));
		dir += dirOffset;
#else
		dir = playerViewAxis[0] + playerViewAxis[2] * (ang * idMath::Sin(spin)) - playerViewAxis[1] * (ang * idMath::Cos(spin));
		dir += dirOffset;
#endif
		//RAVEN END
		dir.Normalize();

		// If a projectile entity has already been created then use that one, otherwise
		// spawn a new one based on the given dictionary
		if (projectileEnt) {
			ent = projectileEnt;
			ent->Show();
			ent->Unbind();
			projectileEnt = NULL;
		}
		else {
			dict.SetInt("instance", owner->GetInstance());
			gameLocal.SpawnEntityDef(dict, &ent, false);
		}

		// Make sure it spawned
		if (!ent) {
			gameLocal.Error("failed to spawn projectile for weapon '%s'", weaponDef->GetName());
		}

		assert(ent->IsType(idProjectile::GetClassType()));

		// Create the projectile
		proj = static_cast<idProjectile*>(ent);
		proj->Create(owner, muzzleOrigin + startOffset, dir, NULL, owner->extraProjPassEntity);

		projBounds = proj->GetPhysics()->GetBounds().Rotate(proj->GetPhysics()->GetAxis());

		// make sure the projectile starts inside the bounding box of the owner
		if (i == 0) {
			idVec3  start;
			float   distance;
			trace_t	tr;
			//RAVEN BEGIN
			//asalmon: xbox must use muzzle Axis for aim assistance
#ifdef _XBOX
			muzzle_pos = muzzleOrigin + muzzleAxis[0] * 2.0f;
			if ((ownerBounds - projBounds).RayIntersection(muzzle_pos, muzzleAxis[0], distance)) {
				start = muzzle_pos + distance * muzzleAxis[0];
			}
#else
			muzzle_pos = muzzleOrigin + playerViewAxis[0] * 2.0f;
			if ((ownerBounds - projBounds).RayIntersection(muzzle_pos, playerViewAxis[0], distance)) {
				start = muzzle_pos + distance * playerViewAxis[0];
			}
#endif
			//RAVEN END
			else {
				start = ownerBounds.GetCenter();
			}
			// RAVEN BEGIN
			// ddynerman: multiple clip worlds
			gameLocal.Translation(owner, tr, start, muzzle_pos, proj->GetPhysics()->GetClipModel(), proj->GetPhysics()->GetClipModel()->GetAxis(), MASK_SHOT_RENDERMODEL, owner);
			// RAVEN END
			muzzle_pos = tr.endpos;
		}

		// Launch the actual projectile
		proj->Launch(muzzle_pos + startOffset, dir, pushVelocity, fuseOffset, power);

		// Increment the projectile launch count and let the derived classes
		// mess with it if they want.
		OnLaunchProjectile(proj);
	}
}

/*
===============================================================================

	States 

===============================================================================
*/

CLASS_STATES_DECLARATION ( rvWeaponRocketLauncher )
	STATE ( "Idle",				rvWeaponRocketLauncher::State_Idle)
	STATE ( "Fire",				rvWeaponRocketLauncher::State_Fire )
	STATE ( "Raise",			rvWeaponRocketLauncher::State_Raise )
	STATE ( "Lower",			rvWeaponRocketLauncher::State_Lower )

	STATE ( "Rocket_Idle",		rvWeaponRocketLauncher::State_Rocket_Idle )
	STATE ( "Rocket_Reload",	rvWeaponRocketLauncher::State_Rocket_Reload )
	
	STATE ( "AddToClip",		rvWeaponRocketLauncher::Frame_AddToClip )
END_CLASS_STATES


/*
================
rvWeaponRocketLauncher::State_Raise

Raise the weapon
================
*/
stateResult_t rvWeaponRocketLauncher::State_Raise ( const stateParms_t& parms ) {
	enum {
		STAGE_INIT,
		STAGE_WAIT,
	};	
	switch ( parms.stage ) {
		// Start the weapon raising
		case STAGE_INIT:
			SetStatus ( WP_RISING );
			PlayAnim( ANIMCHANNEL_LEGS, "raise", 0 );
			return SRESULT_STAGE ( STAGE_WAIT );
			
		case STAGE_WAIT:
			if ( AnimDone ( ANIMCHANNEL_LEGS, 4 ) ) {
				SetState ( "Idle", 4 );
				return SRESULT_DONE;
			}
			if ( wsfl.lowerWeapon ) {
				SetState ( "Lower", 4 );
				return SRESULT_DONE;
			}
			return SRESULT_WAIT;
	}
	return SRESULT_ERROR;
}

/*
================
rvWeaponRocketLauncher::State_Lower

Lower the weapon
================
*/
stateResult_t rvWeaponRocketLauncher::State_Lower ( const stateParms_t& parms ) {	
	enum {
		STAGE_INIT,
		STAGE_WAIT,
		STAGE_WAITRAISE
	};	
	switch ( parms.stage ) {
		case STAGE_INIT:
			SetStatus ( WP_LOWERING );
			PlayAnim ( ANIMCHANNEL_LEGS, "putaway", parms.blendFrames );
			return SRESULT_STAGE(STAGE_WAIT);
			
		case STAGE_WAIT:
			if ( AnimDone ( ANIMCHANNEL_LEGS, 0 ) ) {
				SetStatus ( WP_HOLSTERED );
				return SRESULT_STAGE(STAGE_WAITRAISE);
			}
			return SRESULT_WAIT;
		
		case STAGE_WAITRAISE:
			if ( wsfl.raiseWeapon ) {
				SetState ( "Raise", 0 );
				return SRESULT_DONE;
			}
			return SRESULT_WAIT;
	}
	return SRESULT_ERROR;
}

/*
================
rvWeaponRocketLauncher::State_Idle
================
*/
stateResult_t rvWeaponRocketLauncher::State_Idle( const stateParms_t& parms ) {
	enum {
		STAGE_INIT,
		STAGE_WAIT,
	};	
	switch ( parms.stage ) {
		case STAGE_INIT:
			if ( !AmmoAvailable ( ) ) {
				SetStatus ( WP_OUTOFAMMO );
			} else {
				SetStatus ( WP_READY );
			}
		
			PlayCycle( ANIMCHANNEL_LEGS, "idle", parms.blendFrames );
			return SRESULT_STAGE ( STAGE_WAIT );
		
		case STAGE_WAIT:
			if ( wsfl.lowerWeapon ) {
				SetState ( "Lower", 4 );
				return SRESULT_DONE;
			}		
			if ( gameLocal.time > nextAttackTime && ( gameLocal.isClient || AmmoInClip ( ) ) ) {
				SetState ( "Fire", 2 );
				return SRESULT_DONE;
			}
			return SRESULT_WAIT;
	}
	return SRESULT_ERROR;
}

/*
================
rvWeaponRocketLauncher::State_Fire
================
*/
stateResult_t rvWeaponRocketLauncher::State_Fire ( const stateParms_t& parms ) {
	enum {
		STAGE_INIT,
		STAGE_WAIT,
	};	
	switch ( parms.stage ) {
		case STAGE_INIT:
			nextAttackTime = gameLocal.time + (fireRate * owner->PowerUpModifier ( PMOD_FIRERATE ));		
			Attack ( false, 1, spread, 0, 1.0f );
			PlayAnim ( ANIMCHANNEL_LEGS, "fire", parms.blendFrames );	
			return SRESULT_STAGE ( STAGE_WAIT );
	
		case STAGE_WAIT:			
			if ( wsfl.attack && gameLocal.time >= nextAttackTime && ( gameLocal.isClient || AmmoInClip ( ) ) && !wsfl.lowerWeapon ) {
				SetState ( "Fire", 0 );
				return SRESULT_DONE;
			}
			if ( gameLocal.time > nextAttackTime && AnimDone ( ANIMCHANNEL_LEGS, 4 ) ) {
				SetState ( "Idle", 4 );
				return SRESULT_DONE;
			}
			return SRESULT_WAIT;
	}
	return SRESULT_ERROR;
}

/*
================
rvWeaponRocketLauncher::State_Rocket_Idle
================
*/
stateResult_t rvWeaponRocketLauncher::State_Rocket_Idle ( const stateParms_t& parms ) {
	enum {
		STAGE_INIT,
		STAGE_WAIT,
		STAGE_WAITEMPTY,
	};	
	
	switch ( parms.stage ) {
		case STAGE_INIT:
			if ( AmmoAvailable ( ) <= AmmoInClip() ) {
				PlayAnim( ANIMCHANNEL_TORSO, "idle_empty", parms.blendFrames );
				idleEmpty = true;
			} else { 
				PlayAnim( ANIMCHANNEL_TORSO, "idle", parms.blendFrames );
			}
			return SRESULT_STAGE ( STAGE_WAIT );
		
		case STAGE_WAIT:
			if ( AmmoAvailable ( ) > AmmoInClip() ) {
				if ( idleEmpty ) {
					SetRocketState ( "Rocket_Reload", 0 );
					return SRESULT_DONE;
				} else if ( ClipSize ( ) > 1 ) {
					if ( gameLocal.time > nextAttackTime && AmmoInClip ( ) < ClipSize( ) ) {
						if ( !AmmoInClip() || !wsfl.attack ) {
							SetRocketState ( "Rocket_Reload", 0 );
							return SRESULT_DONE;
						}
					}
				} else {
					if ( AmmoInClip ( ) == 0 ) {
						SetRocketState ( "Rocket_Reload", 0 );
						return SRESULT_DONE;
					}				
				}
			}
			return SRESULT_WAIT;
	}
	return SRESULT_ERROR;
}

/*
================
rvWeaponRocketLauncher::State_Rocket_Reload
================
*/
stateResult_t rvWeaponRocketLauncher::State_Rocket_Reload ( const stateParms_t& parms ) {
	enum {
		STAGE_INIT,
		STAGE_WAIT,
	};	
	
	switch ( parms.stage ) {
		case STAGE_INIT: {
			const char* animName;
			int			animNum;

			if ( idleEmpty ) {
				animName = "ammo_pickup";
				idleEmpty = false;
			} else if ( AmmoAvailable ( ) == AmmoInClip( ) + 1 ) {
				animName = "reload_empty";
			} else {
				animName = "reload";
			}
			
			animNum = viewModel->GetAnimator()->GetAnim ( animName );
			if ( animNum ) {
				idAnim* anim;
				anim = (idAnim*)viewModel->GetAnimator()->GetAnim ( animNum );				
				anim->SetPlaybackRate ( (float)anim->Length() / (reloadRate * owner->PowerUpModifier ( PMOD_FIRERATE )) );
			}

			PlayAnim( ANIMCHANNEL_TORSO, animName, parms.blendFrames );				

			return SRESULT_STAGE ( STAGE_WAIT );
		}
		
		case STAGE_WAIT:
			if ( AnimDone ( ANIMCHANNEL_TORSO, 0 ) ) {				
				if ( !wsfl.attack && gameLocal.time > nextAttackTime && AmmoInClip ( ) < ClipSize( ) && AmmoAvailable() > AmmoInClip() ) {
					SetRocketState ( "Rocket_Reload", 0 );
				} else {
					SetRocketState ( "Rocket_Idle", 0 );
				}
				return SRESULT_DONE;
			}
			/*
			if ( gameLocal.isMultiplayer && gameLocal.time > nextAttackTime && wsfl.attack ) {
				if ( AmmoInClip ( ) == 0 )
				{
					AddToClip ( ClipSize() );
				}
				SetRocketState ( "Rocket_Idle", 0 );
				return SRESULT_DONE;
			}
			*/
			return SRESULT_WAIT;
	}
	return SRESULT_ERROR;
}

/*
================
rvWeaponRocketLauncher::Frame_AddToClip
================
*/
stateResult_t rvWeaponRocketLauncher::Frame_AddToClip ( const stateParms_t& parms ) {
	AddToClip ( 1 );
	return SRESULT_OK;
}

