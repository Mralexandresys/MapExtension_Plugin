#pragma once

#include <map>
#include <string>
#include <vector>

struct IPluginSelf;
struct IPluginHookScanner;

// Etat du jeu lu pour la carte en jeu. Structures POD : publiables vers le rendu.
namespace MapExtensionClient::InGameMap
{
	// Etat d'un point d'interet tel que le jeu le replique pour sa propre carte.
	struct PoiLiveState
	{
		int State = -1;        // EPointOfInterestState : 0 cache, 1 non decouvert, 2 decouvert, 3 desactive ; -1 inconnu
		bool IsBase = false;   // base abandonnee suivie par le jeu : compteurs ci-dessous valides
		// 0 objets a trouver, 1 coffres de plans, 2 objets d'histoire, 3 kystes d'infection
		int Done[4] = {};
		int Total[4] = {};
	};
	// Cle : GUID du marqueur au format du catalogue, "XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX".
	using PoiStateMap = std::map<std::string, PoiLiveState>;

	// Game thread uniquement. Lit ACrMapMenuDataReplicationHelper (etat de decouverte)
	// et ACrAbandonBaseInfoReplicator (collecte par base). Vide si indisponible.
	PoiStateMap CapturePoiStates();

	// Structures lues en jeu, jamais dans le catalogue.
	struct LiveAntenna
	{
		float X = 0.0f, Y = 0.0f;  // monde, cm
		int State = 0;             // ECrAntennaState : 1 infectee, 2 a charger, 3 en charge, 4 a activer, 5 activee, 6 verrouillee
	};
	struct LiveObelisk
	{
		float X = 0.0f, Y = 0.0f;
		int State = -1;            // ECrAlienObeliskState : 0 desactive, 1 recharge, 2 actif, 3 charge, 4 attaque ; -1 inconnu
		float Charge = -1.0f;      // valeur brute du jeu ; < 0 inconnue
		float ChargeMax = -1.0f;   // charge qui declenche l'attaque (trait du jeu) ; <= 0 inconnue
		bool HasAttack = false;    // cible de l'attaque en cours ou prevue
		float AttackX = 0.0f, AttackY = 0.0f;
	};
	struct LiveBaseCore
	{
		float X = 0.0f, Y = 0.0f;  // monde, cm
		std::string Name;          // nom affiche par le jeu, sinon nom unique du batiment
		// ACrBaseCoreReplicationHelper, joint par handle d'entite ; -1 = non joint.
		int Level = -1;            // niveau affiche par le jeu (UpgradeLevel + 1)
		float Cooling = -1.0f, CoolingUsed = -1.0f;
		bool Infected = false;     // sphere d'infection active
		int EntityIndex = 0, EntitySerial = 0;
	};
	struct LiveStructures
	{
		// Base cores : marqueurs de batiment de la carte native dont le nom unique contient "core".
		std::vector<LiveBaseCore> BaseCores;
		int BuildingMarkers = 0;
		int BaseCoreData = 0;       // entrees du jeu
		int BaseCoresJoined = 0;    // base cores ayant recu leur niveau
		std::string BuildingNames;  // noms uniques distincts vus (diagnostic, tronque)
		std::vector<LiveAntenna> Antennas;  // toutes (ACrAntennasOwner, replique)
		// Obelisques : marqueurs de la carte native (ACrMapMenuDataReplicationHelper,
		// etat + cible d'attaque, solo compris), completes par la charge quand le jeu
		// l'expose : bulle Mass repliquee (client multijoueur) ou acteur charge pres du joueur.
		std::vector<LiveObelisk> Obelisks;
		int ObeliskMarkers = 0;
		int ObeliskActors = 0;
		int ObeliskReplicated = 0;
		// Lecture native des entites Mass (solo compris) : nombre d'obelisques trouves,
		// -1 si la fonctionnalite est coupee (signature absente ou lecture en echec).
		int ObeliskNative = -1;
		int EntitiesScanned = 0;
	};

	// A appeler depuis OnPluginLoadHooks uniquement. Demande au loader la fonction
	// et le champ natifs qui donnent acces aux fragments Mass ; un echec coupe la
	// seule lecture native des obelisques, jamais le plugin.
	void ResolveNativeAccess(IPluginSelf* self, IPluginHookScanner* scanner);
	// Game thread uniquement. Parcourt la liste d'acteurs : a appeler rarement.
	LiveStructures CaptureLiveStructures();

	// Game thread uniquement. Niveau de radiation replique pour la carte native ; -1 si inconnu.
	int ReadRadiationLevel();
}
