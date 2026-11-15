#pragma once

// Projection grille terrain native <-> monde (POC carte in-game, jalon J1).
// Pur et sans dependance : utilisable depuis n'importe quel thread.
//
// L'ecran suit la convention du viewer web (map_state_types.h) : x ecran croit
// avec X monde, y ecran croit avec Y monde. Seule inconnue : quel index de
// grille suit quel axe monde (`swapAxes`), a prouver en jeu (journal J1).

namespace MapExtensionClient::InGameMap
{
	struct TerrainGrid
	{
		float PivotX = 0.0f;    // MapTerrainTopLeftPivotPoint
		float PivotY = 0.0f;
		float SegmentX = 0.0f;  // MapTerrainSegmentSize (peut etre negatif)
		float SegmentY = 0.0f;
	};

	// swapAxes=false : index.X suit X monde. swapAxes=true : index.X suit Y monde
	// (la "transposition" observee sur feature/new-ui).
	constexpr void SegmentToWorld(const TerrainGrid& grid, float i, float j, bool swapAxes, float& worldX, float& worldY)
	{
		worldX = grid.PivotX + (swapAxes ? j : i) * grid.SegmentX;
		worldY = grid.PivotY + (swapAxes ? i : j) * grid.SegmentY;
	}

	constexpr void WorldToSegment(const TerrainGrid& grid, float worldX, float worldY, bool swapAxes, float& i, float& j)
	{
		const float alongX = grid.SegmentX != 0.0f ? (worldX - grid.PivotX) / grid.SegmentX : 0.0f;
		const float alongY = grid.SegmentY != 0.0f ? (worldY - grid.PivotY) / grid.SegmentY : 0.0f;
		i = swapAxes ? alongY : alongX;
		j = swapAxes ? alongX : alongY;
	}

	namespace ProjectionSelfCheck
	{
		constexpr bool RoundTrip(bool swapAxes)
		{
			const TerrainGrid grid{ -400000.0f, 300000.0f, 100000.0f, -50000.0f };
			float wx = 0.0f, wy = 0.0f, i = 0.0f, j = 0.0f;
			SegmentToWorld(grid, 2.0f, 5.0f, swapAxes, wx, wy);
			WorldToSegment(grid, wx, wy, swapAxes, i, j);
			return i == 2.0f && j == 5.0f;
		}
		static_assert(RoundTrip(false) && RoundTrip(true), "la projection grille <-> monde doit etre reversible");
	}
}
