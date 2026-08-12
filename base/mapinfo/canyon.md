
#include <mapinfo/mapinfo.include>

mapInfoDef "canyon" {
	_default_mapinfo( "canyon" )

	data {
		"script_entrypoint"		"Canyon_MapScript"
		"mapBriefing"			"maps/canyon/map_breifing"
		"mapLocation"			"maps/canyon/map_location"
		"campaignDescription"	"maps/canyon/campaign_description"
		"numObjectives"			"4"
		"mtr_serverShot"		"levelshots/canyon"
		"mtr_backdrop"			"levelshots/campaigns/pacific"		
		"mapPosition"			"352 345"
		"snd_music"				"sounds/music/load1"
		"strogg_endgame_pause"	"5.0"
		"gdf_endgame_pause"		"6.0"
	}
}