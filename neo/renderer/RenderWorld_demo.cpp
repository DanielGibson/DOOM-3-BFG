/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#pragma hdrstop
#include "precompiled.h"

#include "tr_local.h"

//#define WRITE_GUIS

typedef struct
{
	int		version;
	int		sizeofRenderEntity;
	int		sizeofRenderLight;
	char	mapname[256];
} demoHeader_t;


/*
==============
StartWritingDemo
==============
*/
void		idRenderWorldLocal::StartWritingDemo( idDemoFile* demo )
{
	int		i;
	
	// FIXME: we should track the idDemoFile locally, instead of snooping into session for it
	
	WriteLoadMap();
	
	// write the door portal state
	for( i = 0 ; i < numInterAreaPortals ; i++ )
	{
		if( doublePortals[i].blockingBits )
		{
			SetPortalState( i + 1, doublePortals[i].blockingBits );
		}
	}
	
	// clear the archive counter on all defs
	for( i = 0 ; i < lightDefs.Num() ; i++ )
	{
		if( lightDefs[i] )
		{
			lightDefs[i]->archived = false;
		}
	}
	for( i = 0 ; i < entityDefs.Num() ; i++ )
	{
		if( entityDefs[i] )
		{
			entityDefs[i]->archived = false;
		}
	}
}

void idRenderWorldLocal::StopWritingDemo()
{
//	writeDemo = NULL;
}

bool idRenderWorldLocal::LoadDemoMap(const char* mapname)
{
	// TODO: maybe map loading from mapname could be unified here, in load savegame, in start map, etc?

	if( fileSystem->UsingResourceFiles() )
	{
		// each map has its own .resources file - load it
		idStrStatic< MAX_OSPATH > currentMapName = mapname;
		currentMapName.StripFileExtension();
		idStrStatic< MAX_OSPATH > manifestName = currentMapName;
		manifestName.Replace( "game/", "maps/" );
		manifestName.Replace( "maps/maps/", "maps/" );
		manifestName.Replace( "/mp/", "/" );
		manifestName += ".preload";
		common->Printf("## manifestName: %s currentMapName: %s\n", manifestName.c_str(), currentMapName.c_str());
		idPreloadManifest manifest;
		manifest.LoadManifest( manifestName );
		renderSystem->Preload( manifest, currentMapName );
		common->Printf("## Finished preloading renderstuff\n");
		soundSystem->Preload( manifest );
		game->Preload( manifest );
	}

	if( !InitFromMap( mapname ) )
	{
		common->Warning( "DC_LOADMAP: InitFromMap(%s) failed!\n", mapname );
	}
}

/*
==============
ProcessDemoCommand
==============
*/
bool		idRenderWorldLocal::ProcessDemoCommand( idDemoFile* readDemo, renderView_t* renderView, int* demoTimeOffset )
{
	bool	newMap = false;
	
	if( !readDemo )
	{
		return false;
	}
	
	demoCommand_t	dc;
	qhandle_t		h;
	
	if( !readDemo->ReadInt( ( int& )dc ) )
	{
		// a demoShot may not have an endFrame, but it is still valid
		return false;
	}
	
	// common->Printf( "ProcessDemoCommand: %d\n",  dc );
	
	switch( dc )
	{
		case DC_LOADMAP:
			// read the initial data
			demoHeader_t	header;
			
			readDemo->ReadInt( header.version );
			readDemo->ReadInt( header.sizeofRenderEntity );
			readDemo->ReadInt( header.sizeofRenderLight );
			for( int i = 0; i < 256; i++ )
				readDemo->ReadChar( header.mapname[i] );
			// the internal version value got replaced by DS_VERSION at toplevel
			if( header.version != 4 )
			{
				common->Error( "Demo version mismatch.\n" );
			}
			
			if( r_showDemo.GetBool() )
			{
				common->Printf( "DC_LOADMAP: %s\n", header.mapname );
			}

			LoadDemoMap(header.mapname); // TODO: handle return value
			
			newMap = true;		// we will need to set demoTimeOffset
			
			break;
			
		case DC_RENDERVIEW:
			// idLib::Printf( "### idRenderWorldLocal::ProcessDemoCommand() DC_RENDERVIEW\n" );
			readDemo->ReadInt( renderView->viewID );
			readDemo->ReadFloat( renderView->fov_x );
			readDemo->ReadFloat( renderView->fov_y );
			readDemo->ReadVec3( renderView->vieworg );
			readDemo->ReadMat3( renderView->viewaxis );
			readDemo->ReadBool( renderView->cramZNear );
			readDemo->ReadBool( renderView->forceUpdate );
			readDemo->ReadInt( renderView->time[1] );
			for( int i = 0; i < MAX_GLOBAL_SHADER_PARMS; i++ )
				readDemo->ReadFloat( renderView->shaderParms[i] );
				
			if( !readDemo->ReadFakePtr( renderView->globalMaterial ) )
			{
				// DG: whatever strange kind of case this is supposed to handle...
				renderView->globalMaterial = NULL;
				return false;
			}
			
			// make sure it doesn't point into the void
			renderView->globalMaterial = NULL;
			//renderView->globalMaterial = (idMaterial*)1;// NULL; // FIXME
			
			if( r_showDemo.GetBool() )
			{
				common->Printf( "DC_RENDERVIEW: %i\n", renderView->time );
			}
			
			// possibly change the time offset if this is from a new map
			if( newMap && demoTimeOffset )
			{
				*demoTimeOffset = renderView->time[1] - eventLoop->Milliseconds();
			}
			return false;
			
		case DC_UPDATE_ENTITYDEF:
			ReadRenderEntity();
			break;
		case DC_DELETE_ENTITYDEF:
			if( !readDemo->ReadInt( h ) )
			{
				return false;
			}
			if( r_showDemo.GetBool() )
			{
				common->Printf( "DC_DELETE_ENTITYDEF: %i\n", h );
			}
			FreeEntityDef( h );
			break;
		case DC_UPDATE_LIGHTDEF:
			ReadRenderLight();
			break;
		case DC_DELETE_LIGHTDEF:
			if( !readDemo->ReadInt( h ) )
			{
				return false;
			}
			if( r_showDemo.GetBool() )
			{
				common->Printf( "DC_DELETE_LIGHTDEF: %i\n", h );
			}
			FreeLightDef( h );
			break;
			
		case DC_CAPTURE_RENDER:
			if( r_showDemo.GetBool() )
			{
				common->Printf( "DC_CAPTURE_RENDER\n" );
			}
			renderSystem->CaptureRenderToImage( readDemo->ReadHashString() );
			break;
			
		case DC_CROP_RENDER:
			if( r_showDemo.GetBool() )
			{
				common->Printf( "DC_CROP_RENDER\n" );
			}
			int	size[2];
			readDemo->ReadInt( size[0] );
			readDemo->ReadInt( size[1] );
			// readDemo->ReadInt( size[2] ); it only wrote width and height - right?
			renderSystem->CropRenderSize( size[0], size[1] );
			break;
			
		case DC_UNCROP_RENDER:
			if( r_showDemo.GetBool() )
			{
				common->Printf( "DC_UNCROP\n" );
			}
			renderSystem->UnCrop();
			break;
			
		case DC_GUI_MODEL:
			if( r_showDemo.GetBool() )
			{
				common->Printf( "DC_GUI_MODEL\n" );
			}
			break;
			
		case DC_DEFINE_MODEL:
		{
			idRenderModel*	model = renderModelManager->AllocModel();
			model->ReadFromDemoFile( common->ReadDemo() );
			// add to model manager, so we can find it
			renderModelManager->AddModel( model );
			
			// save it in the list to free when clearing this map
			localModels.Append( model );
			
			if( r_showDemo.GetBool() )
			{
				common->Printf( "DC_DEFINE_MODEL\n" );
			}
			break;
		}
		case DC_SET_PORTAL_STATE:
		{
			int		data[2];
			readDemo->ReadInt( data[0] );
			readDemo->ReadInt( data[1] );
			SetPortalState( data[0], data[1] );
			if( r_showDemo.GetBool() )
			{
				common->Printf( "DC_SET_PORTAL_STATE: %i %i\n", data[0], data[1] );
			}
		}
		
		break;
		case DC_END_FRAME:
			return true;
			
		default:
			common->Error( "Bad token in demo stream" );
	}
	
	return false;
}

/*
================
WriteLoadMap
================
*/
void	idRenderWorldLocal::WriteLoadMap()
{

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if( this != common->RW() )
	{
		return;
	}
	
	common->WriteDemo()->WriteInt( DS_RENDER );
	common->WriteDemo()->WriteInt( DC_LOADMAP );
	
	demoHeader_t	header;
	strncpy( header.mapname, mapName.c_str(), sizeof( header.mapname ) - 1 );
	header.version = 4;
	header.sizeofRenderEntity = sizeof( renderEntity_t );
	header.sizeofRenderLight = sizeof( renderLight_t );
	common->WriteDemo()->WriteInt( header.version );
	common->WriteDemo()->WriteInt( header.sizeofRenderEntity );
	common->WriteDemo()->WriteInt( header.sizeofRenderLight );
	for( int i = 0; i < 256; i++ )
		common->WriteDemo()->WriteChar( header.mapname[i] );
		
	if( r_showDemo.GetBool() )
	{
		common->Printf( "write DC_DELETE_LIGHTDEF: %s\n", mapName.c_str() );
	}
}

/*
================
WriteVisibleDefs

================
*/
void	idRenderWorldLocal::WriteVisibleDefs( const viewDef_t* viewDef )
{
	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if( this != common->RW() )
	{
		return;
	}
	
	// make sure all necessary entities and lights are updated
	for( viewEntity_t* viewEnt = viewDef->viewEntitys ; viewEnt ; viewEnt = viewEnt->next )
	{
		idRenderEntityLocal* ent = viewEnt->entityDef;
		
		// DG: add check for ent == NULL - happens if the viewEntity_t is from idGuiModel::EmitSurfaces()
		if( ent == NULL || ent->archived )
		{
			// still up to date
			continue;
		}
		
		// write it out
		WriteRenderEntity( ent->index, &ent->parms );
		ent->archived = true;
	}
	
	for( viewLight_t* viewLight = viewDef->viewLights ; viewLight ; viewLight = viewLight->next )
	{
		idRenderLightLocal* light = viewLight->lightDef;
		
		if( light->archived )
		{
			// still up to date
			continue;
		}
		// write it out
		WriteRenderLight( light->index, &light->parms );
		light->archived = true;
	}
}


/*
================
WriteRenderView
================
*/
void	idRenderWorldLocal::WriteRenderView( const renderView_t* renderView )
{
	int i;
	
	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if( this != common->RW() )
	{
		return;
	}
	
	assert( renderView->fov_x > 0.0f && renderView->fov_y > 0.0f );
	
	// write the actual view command
	common->WriteDemo()->WriteInt( DS_RENDER );
	common->WriteDemo()->WriteInt( DC_RENDERVIEW );
	common->WriteDemo()->WriteInt( renderView->viewID );
	common->WriteDemo()->WriteFloat( renderView->fov_x );
	common->WriteDemo()->WriteFloat( renderView->fov_y );
	common->WriteDemo()->WriteVec3( renderView->vieworg );
	common->WriteDemo()->WriteMat3( renderView->viewaxis );
	common->WriteDemo()->WriteBool( renderView->cramZNear );
	common->WriteDemo()->WriteBool( renderView->forceUpdate );
	common->WriteDemo()->WriteInt( renderView->time[1] );
	for( i = 0; i < MAX_GLOBAL_SHADER_PARMS; i++ )
		common->WriteDemo()->WriteFloat( renderView->shaderParms[i] );
		
	common->WriteDemo()->WriteFakePtr( renderView->globalMaterial );
	
	if( r_showDemo.GetBool() )
	{
		common->Printf( "write DC_RENDERVIEW: %i\n", renderView->time );
	}
}

/*
================
WriteFreeEntity
================
*/
void	idRenderWorldLocal::WriteFreeEntity( qhandle_t handle )
{

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if( this != common->RW() )
	{
		return;
	}
	
	common->WriteDemo()->WriteInt( DS_RENDER );
	common->WriteDemo()->WriteInt( DC_DELETE_ENTITYDEF );
	common->WriteDemo()->WriteInt( handle );
	
	if( r_showDemo.GetBool() )
	{
		common->Printf( "write DC_DELETE_ENTITYDEF: %i\n", handle );
	}
}

/*
================
WriteFreeLightEntity
================
*/
void	idRenderWorldLocal::WriteFreeLight( qhandle_t handle )
{

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if( this != common->RW() )
	{
		return;
	}
	
	common->WriteDemo()->WriteInt( DS_RENDER );
	common->WriteDemo()->WriteInt( DC_DELETE_LIGHTDEF );
	common->WriteDemo()->WriteInt( handle );
	
	if( r_showDemo.GetBool() )
	{
		common->Printf( "write DC_DELETE_LIGHTDEF: %i\n", handle );
	}
}

/*
================
WriteRenderLight
================
*/
void	idRenderWorldLocal::WriteRenderLight( qhandle_t handle, const renderLight_t* light )
{

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if( this != common->RW() )
	{
		return;
	}
	
	common->WriteDemo()->WriteInt( DS_RENDER );
	common->WriteDemo()->WriteInt( DC_UPDATE_LIGHTDEF );
	common->WriteDemo()->WriteInt( handle );
	
	common->WriteDemo()->WriteMat3( light->axis );
	common->WriteDemo()->WriteVec3( light->origin );
	common->WriteDemo()->WriteInt( light->suppressLightInViewID );
	common->WriteDemo()->WriteInt( light->allowLightInViewID );
	common->WriteDemo()->WriteBool( light->noShadows );
	common->WriteDemo()->WriteBool( light->noSpecular );
	common->WriteDemo()->WriteBool( light->pointLight );
	common->WriteDemo()->WriteBool( light->parallel );
	common->WriteDemo()->WriteVec3( light->lightRadius );
	common->WriteDemo()->WriteVec3( light->lightCenter );
	common->WriteDemo()->WriteVec3( light->target );
	common->WriteDemo()->WriteVec3( light->right );
	common->WriteDemo()->WriteVec3( light->up );
	common->WriteDemo()->WriteVec3( light->start );
	common->WriteDemo()->WriteVec3( light->end );
	// DG: use WriteFakePtr for pointers
	common->WriteDemo()->WriteFakePtr( light->prelightModel );
	common->WriteDemo()->WriteInt( light->lightId );
	common->WriteDemo()->WriteFakePtr( light->shader );
	for( int i = 0; i < MAX_ENTITY_SHADER_PARMS; i++ )
		common->WriteDemo()->WriteFloat( light->shaderParms[i] );
	common->WriteDemo()->WriteFakePtr( light->referenceSound );
	// DG end
	
	if( light->prelightModel )
	{
		common->WriteDemo()->WriteHashString( light->prelightModel->Name() );
	}
	if( light->shader )
	{
		common->WriteDemo()->WriteHashString( light->shader->GetName() );
	}
	if( light->referenceSound )
	{
		int	index = light->referenceSound->Index();
		common->WriteDemo()->WriteInt( index );
	}
	
	if( r_showDemo.GetBool() )
	{
		common->Printf( "write DC_UPDATE_LIGHTDEF: %i\n", handle );
	}
}

/*
================
ReadRenderLight
================
*/
void	idRenderWorldLocal::ReadRenderLight( )
{
	renderLight_t	light;
	int				index;
	
	common->ReadDemo()->ReadInt( index );
	if( index < 0 )
	{
		common->Error( "ReadRenderLight: index < 0 " );
	}
	
	common->ReadDemo()->ReadMat3( light.axis );
	common->ReadDemo()->ReadVec3( light.origin );
	common->ReadDemo()->ReadInt( light.suppressLightInViewID );
	common->ReadDemo()->ReadInt( light.allowLightInViewID );
	common->ReadDemo()->ReadBool( light.noShadows );
	common->ReadDemo()->ReadBool( light.noSpecular );
	common->ReadDemo()->ReadBool( light.pointLight );
	common->ReadDemo()->ReadBool( light.parallel );
	common->ReadDemo()->ReadVec3( light.lightRadius );
	common->ReadDemo()->ReadVec3( light.lightCenter );
	common->ReadDemo()->ReadVec3( light.target );
	common->ReadDemo()->ReadVec3( light.right );
	common->ReadDemo()->ReadVec3( light.up );
	common->ReadDemo()->ReadVec3( light.start );
	common->ReadDemo()->ReadVec3( light.end );
	// DG: using ReadFakePtr() instead of ReadInt() to get information if ppinters were NULL
	common->ReadDemo()->ReadFakePtr( light.prelightModel );
	common->ReadDemo()->ReadInt( light.lightId );
	common->ReadDemo()->ReadFakePtr( light.shader );
	for( int i = 0; i < MAX_ENTITY_SHADER_PARMS; i++ )
		common->ReadDemo()->ReadFloat( light.shaderParms[i] );
	common->ReadDemo()->ReadFakePtr( light.referenceSound );
	// DG end
	if( light.prelightModel )
	{
		light.prelightModel = renderModelManager->FindModel( common->ReadDemo()->ReadHashString() );
	}
	if( light.shader )
	{
		light.shader = declManager->FindMaterial( common->ReadDemo()->ReadHashString() );
	}
	if( light.referenceSound )
	{
		int	index;
		common->ReadDemo()->ReadInt( index );
		light.referenceSound = common->SW()->EmitterForIndex( index );
	}
	
	UpdateLightDef( index, &light );
	
	if( r_showDemo.GetBool() )
	{
		common->Printf( "DC_UPDATE_LIGHTDEF: %i\n", index );
	}
}

/*
================
WriteRenderEntity
================
*/
void	idRenderWorldLocal::WriteRenderEntity( qhandle_t handle, const renderEntity_t* ent )
{

	// only the main renderWorld writes stuff to demos, not the wipes or
	// menu renders
	if( this != common->RW() )
	{
		return;
	}
	
	common->WriteDemo()->WriteInt( DS_RENDER );
	common->WriteDemo()->WriteInt( DC_UPDATE_ENTITYDEF );
	common->WriteDemo()->WriteInt( handle );
	
	// DG: replaced WriteInt() hackery with pointers with WriteFakePtr()
	common->WriteDemo()->WriteFakePtr( ent->hModel );
	common->WriteDemo()->WriteInt( ent->entityNum );
	common->WriteDemo()->WriteInt( ent->bodyId );
	common->WriteDemo()->WriteVec3( ent->bounds[0] );
	common->WriteDemo()->WriteVec3( ent->bounds[1] );
	// DG: don't save the ent->callback* stuff, it's set to NULL on read anyway..
	common->WriteDemo()->WriteInt( ent->suppressSurfaceInViewID );
	common->WriteDemo()->WriteInt( ent->suppressShadowInViewID );
	common->WriteDemo()->WriteInt( ent->suppressShadowInLightID );
	common->WriteDemo()->WriteInt( ent->allowSurfaceInViewID );
	common->WriteDemo()->WriteVec3( ent->origin );
	common->WriteDemo()->WriteMat3( ent->axis );
	common->WriteDemo()->WriteFakePtr( ent->customShader );
	common->WriteDemo()->WriteFakePtr( ent->referenceShader );
	common->WriteDemo()->WriteFakePtr( ent->customSkin );
	common->WriteDemo()->WriteFakePtr( ent->referenceSound );
	for( int i = 0; i < MAX_ENTITY_SHADER_PARMS; i++ )
		common->WriteDemo()->WriteFloat( ent->shaderParms[i] );
	for( int i = 0; i < MAX_RENDERENTITY_GUI; i++ )
		common->WriteDemo()->WriteFakePtr( ent->gui[i] );
	// DG: don't save remoteRenderView, it's not restored properly anyway
	common->WriteDemo()->WriteInt( ent->numJoints );
	// DG: no reason to store/read ent.joints, they're replaced below anyway
	common->WriteDemo()->WriteFloat( ent->modelDepthHack );
	common->WriteDemo()->WriteBool( ent->noSelfShadow );
	common->WriteDemo()->WriteBool( ent->noShadow );
	common->WriteDemo()->WriteBool( ent->noDynamicInteractions );
	common->WriteDemo()->WriteBool( ent->weaponDepthHack );
	common->WriteDemo()->WriteInt( ent->forceUpdate );
	
	if( ent->customShader )
	{
		common->WriteDemo()->WriteHashString( ent->customShader->GetName() );
	}
	if( ent->customSkin )
	{
		common->WriteDemo()->WriteHashString( ent->customSkin->GetName() );
	}
	if( ent->hModel )
	{
		common->WriteDemo()->WriteHashString( ent->hModel->Name() );
	}
	if( ent->referenceShader )
	{
		common->WriteDemo()->WriteHashString( ent->referenceShader->GetName() );
	}
	if( ent->referenceSound )
	{
		int	index = ent->referenceSound->Index();
		common->WriteDemo()->WriteInt( index );
	}
	if( ent->numJoints )
	{
		for( int i = 0; i < ent->numJoints; i++ )
		{
			float* data = ent->joints[i].ToFloatPtr();
			for( int j = 0; j < 12; ++j )
				common->WriteDemo()->WriteFloat( data[j] );
		}
	}
	
	/*
	if ( ent->decals ) {
		ent->decals->WriteToDemoFile( common->ReadDemo() );
	}
	if ( ent->overlays ) {
		ent->overlays->WriteToDemoFile( common->WriteDemo() );
	}
	*/
	
#ifdef WRITE_GUIS
	if( ent->gui )
	{
		ent->gui->WriteToDemoFile( common->WriteDemo() );
	}
	if( ent->gui2 )
	{
		ent->gui2->WriteToDemoFile( common->WriteDemo() );
	}
	if( ent->gui3 )
	{
		ent->gui3->WriteToDemoFile( common->WriteDemo() );
	}
#endif
	
	// RENDERDEMO_VERSION >= 2 ( Doom3 1.2 )
	common->WriteDemo()->WriteInt( ent->timeGroup );
	common->WriteDemo()->WriteInt( ent->xrayIndex );
	
	if( r_showDemo.GetBool() )
	{
		common->Printf( "write DC_UPDATE_ENTITYDEF: %i = %s\n", handle, ent->hModel ? ent->hModel->Name() : "NULL" );
	}
}

/*
================
ReadRenderEntity
================
*/
void	idRenderWorldLocal::ReadRenderEntity()
{
	renderEntity_t		ent;
	int				index, i;
	
	common->ReadDemo()->ReadInt( index );
	if( index < 0 )
	{
		common->Error( "ReadRenderEntity: index < 0" );
	}
	
	// DG: replaced ReadInt() hackery with pointers with ReadFakePtr()
	common->ReadDemo()->ReadFakePtr( ent.hModel );
	common->ReadDemo()->ReadInt( ent.entityNum );
	common->ReadDemo()->ReadInt( ent.bodyId );
	common->ReadDemo()->ReadVec3( ent.bounds[0] );
	common->ReadDemo()->ReadVec3( ent.bounds[1] );
	// DG: the following were set to NULL anyway, so why bother to save/restore them..
	ent.callback = NULL;
	ent.callbackData = NULL;
	common->ReadDemo()->ReadInt( ent.suppressSurfaceInViewID );
	common->ReadDemo()->ReadInt( ent.suppressShadowInViewID );
	common->ReadDemo()->ReadInt( ent.suppressShadowInLightID );
	common->ReadDemo()->ReadInt( ent.allowSurfaceInViewID );
	common->ReadDemo()->ReadVec3( ent.origin );
	common->ReadDemo()->ReadMat3( ent.axis );
	common->ReadDemo()->ReadFakePtr( ent.customShader );
	common->ReadDemo()->ReadFakePtr( ent.referenceShader );
	common->ReadDemo()->ReadFakePtr( ent.customSkin );
	common->ReadDemo()->ReadFakePtr( ent.referenceSound );
	for( i = 0; i < MAX_ENTITY_SHADER_PARMS; i++ )
	{
		common->ReadDemo()->ReadFloat( ent.shaderParms[i] );
	}
	for( i = 0; i < MAX_RENDERENTITY_GUI; i++ )
	{
		common->ReadDemo()->ReadFakePtr( ent.gui[i] );
	}
	// DG: don't save/restore remoteRenderView, just set it to NULL
	//     because the pointer wouldn't be valid anyway
	ent.remoteRenderView = NULL;
	common->ReadDemo()->ReadInt( ent.numJoints );
	// DG: no reason to store/read ent.joints, they're replaced below anyway
	common->ReadDemo()->ReadFloat( ent.modelDepthHack );
	common->ReadDemo()->ReadBool( ent.noSelfShadow );
	common->ReadDemo()->ReadBool( ent.noShadow );
	common->ReadDemo()->ReadBool( ent.noDynamicInteractions );
	common->ReadDemo()->ReadBool( ent.weaponDepthHack );
	common->ReadDemo()->ReadInt( ent.forceUpdate );
	
	if( ent.customShader )
	{
		ent.customShader = declManager->FindMaterial( common->ReadDemo()->ReadHashString() );
	}
	if( ent.customSkin )
	{
		ent.customSkin = declManager->FindSkin( common->ReadDemo()->ReadHashString() );
	}
	if( ent.hModel )
	{
		ent.hModel = renderModelManager->FindModel( common->ReadDemo()->ReadHashString() );
	}
	if( ent.referenceShader )
	{
		ent.referenceShader = declManager->FindMaterial( common->ReadDemo()->ReadHashString() );
	}
	if( ent.referenceSound )
	{
		int	index;
		common->ReadDemo()->ReadInt( index );
		ent.referenceSound = common->SW()->EmitterForIndex( index );
	}
	if( ent.numJoints )
	{
		ent.joints = ( idJointMat* )Mem_Alloc16( SIMD_ROUND_JOINTS( ent.numJoints ) * sizeof( ent.joints[0] ), TAG_JOINTMAT );
		for( int i = 0; i < ent.numJoints; i++ )
		{
			float* data = ent.joints[i].ToFloatPtr();
			for( int j = 0; j < 12; ++j )
			{
				common->ReadDemo()->ReadFloat( data[j] );
			}
		}
		SIMD_INIT_LAST_JOINT( ent.joints, ent.numJoints );
	} else {
		ent.joints = NULL;
	}
	
	
	
	/*
	if ( ent.decals ) {
		ent.decals = idRenderModelDecal::Alloc();
		ent.decals->ReadFromDemoFile( common->ReadDemo() );
	}
	if ( ent.overlays ) {
		ent.overlays = idRenderModelOverlay::Alloc();
		ent.overlays->ReadFromDemoFile( common->ReadDemo() );
	}
	*/
	
	for( i = 0; i < MAX_RENDERENTITY_GUI; i++ )
	{
		if( ent.gui[ i ] )
		{
			ent.gui[ i ] = uiManager->Alloc();
#ifdef WRITE_GUIS
			ent.gui[ i ]->ReadFromDemoFile( common->ReadDemo() );
#endif
		}
	}
	
	common->ReadDemo()->ReadInt( ent.timeGroup );
	common->ReadDemo()->ReadInt( ent.xrayIndex );
	
	UpdateEntityDef( index, &ent );
	
	if( r_showDemo.GetBool() )
	{
		common->Printf( "DC_UPDATE_ENTITYDEF: %i = %s\n", index, ent.hModel ? ent.hModel->Name() : "NULL" );
	}
}
