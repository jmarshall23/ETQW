
#include <mapinfo/mapinfo.include>

mapInfoDef "outskirts" {
	_default_mapinfo( "outskirts" )

	data {
		"script_entrypoint"		"OutSkirts_MapScript"
		"mapBriefing"			"maps/outskirts/map_briefing"
		"mapLocation"			"maps/outskirts/map_location"
		"campaignDescription"	"maps/outskirts/campaign_description"
		"numObjectives"			"4"
		"mtr_serverShot"		"levelshots/outskirts"
		"mtr_backDrop"			"levelshots/campaigns/northamerica"
		"mapPosition"			"529 135"
		"snd_music"				"sounds/music/load2"
		"strogg_endgame_pause"	"5.0"
		"gdf_endgame_pause"		"5.0"
		// map position:
		// relative to a 640 x 480 screen - "0 0" is the top left corner
		// subtract 9 from each value to centre the icon properly
	}
}