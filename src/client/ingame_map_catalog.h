#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Catalogue statique du monde (ressources et points d'interet), lu depuis
// `map-data/manifest.js`, `resources-*.js` et `pois.js`, generes par tools/build_map_data.py. Aucun UObject, aucun acces
// au jeu : chargeable depuis n'importe quel thread.

namespace MapExtensionClient::InGameMap
{
	enum class CatalogCategory : uint8_t { Poi, Mineral, Plant, Creature };

	struct CatalogPoint
	{
		float X = 0.0f;  // monde, cm
		float Y = 0.0f;
	};

	// Un type de ressource (ex. titane) ou un groupe de points d'interet (ex. grottes).
	struct CatalogGroup
	{
		CatalogCategory Category = CatalogCategory::Poi;
		std::string Label;
		std::string ItemClassPath;        // classe d'item du jeu (icone), vide si inconnue
		std::vector<CatalogPoint> Points;
		std::vector<uint8_t> Purity;      // vide ou aligne sur Points : 0 inconnu, 1 impur, 2 normal, 3 pur
		std::vector<std::string> Names;   // vide ou aligne sur Points (points d'interet nommes)
		std::vector<std::string> Guids;   // vide ou aligne sur Points : GUID du marqueur de POI du jeu
	};

	struct Catalog
	{
		std::vector<CatalogGroup> Groups;  // tries par categorie
		size_t PointCount = 0;
		std::string Error;                 // non vide si le chargement a echoue
	};

	const char* PurityName(uint8_t purity);

	// Dossier attendu : `<dossier de la DLL>\MapExtension\map-data`.
	std::wstring DefaultCatalogDirectory();

	// Ne lance jamais d'exception : en cas d'echec, `Error` est renseigne.
	std::shared_ptr<const Catalog> LoadCatalog(const std::wstring& directory);
}
