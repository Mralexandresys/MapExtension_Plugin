#pragma once

#include "ingame_map_projection.h"

#include <map>
#include <string>
#include <vector>

namespace MapExtensionClient::InGameMap
{
	struct TerrainSegment
	{
		int GridX = 0;
		int GridY = 0;
		std::string R1;  // ressource de TerrainSegmentTexture
		std::string R2;  // ressource de TerrainSegmentTextureRadiation2
	};

	// Descripteur POD du terrain natif : aucun UObject, publiable vers le render thread.
	struct TerrainDescriptor
	{
		bool Valid = false;
		TerrainGrid Grid;
		int MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
		int RadiationLevel = -1;  // -1 = inconnu
		std::vector<TerrainSegment> Segments;
	};

	// Game thread uniquement. Re-resout tout depuis le CDO a chaque appel.
	// `logDetails` : ecrit l'inventaire complet (J1) dans le log du loader.
	bool CaptureTerrain(TerrainDescriptor& out, bool logDetails);

	// Game thread uniquement. Copie GPU du mip actuellement resident de la tuile
	// (LoadFromUTexture2D) ; retourne un PluginTextureHandle ou nullptr.
	// `textureName` est la cle de partage du loader : une FreeTexture par appel reussi.
	void* LoadSegmentTexture(int gridX, int gridY, bool useR2, const char* textureName);

	// Game thread uniquement. Piste A (plan J2) : demande au streaming de garder
	// tous les mips de la tuile residents pendant `seconds`.
	// Game thread uniquement. Charge la classe d'item (ex.
	// "/Game/Chimera/Items/I_TitaniumOre.I_TitaniumOre_C") et copie son icone.
	// Retourne un PluginTextureHandle ou nullptr.
	void* LoadItemIcon(const char* itemClassPath, const char* textureName);

	// Game thread uniquement. Charge une texture du jeu par son chemin d'asset (ex.
	// "/Game/Chimera/UI/Map/Markers/T_UI_antenna_mapIcon.T_UI_antenna_mapIcon") et la copie.
	// `reason` (optionnel) : cause de l'echec. "not resident" = texture chargee mais
	// pas encore sur le GPU : ses mips sont demandes, reessayer peu apres.
	void* LoadTextureAsset(const char* assetPath, const char* textureName, const char** reason = nullptr);

	bool RequestSegmentMips(int gridX, int gridY, bool useR2, float seconds);

	// Game thread uniquement. Icones d'items deja charges par le jeu, retrouves
	// par nom de classe ("I_StarTears_C") ou par nom affiche (celui du snapshot
	// cargo). Un seul parcours de GObjects pour toutes les cles ; resultat aligne
	// sur `keys`, nullptr si l'item n'est pas charge ou n'a pas d'icone.
	std::vector<void*> LoadItemIconsByName(const std::vector<std::string>& keys);
}
