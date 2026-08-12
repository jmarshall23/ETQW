
#include <mapinfo/mapinfo.include>

/*
//========================================================================
// Test stuff and odd bits
//========================================================================
mapMetaData "maps/commandMap_test" {
	"mapinfo"				"commandMap_test"
	"dz_deployInfo"			"commandMap_test"
}

//========================================================================
mapMetaData "maps/digibob/magog_test" {
	"mapinfo"				"valley"
	"dz_deployInfo"			"valley"
}

//========================================================================
mapMetaData "maps/road_demo" {
	"mapinfo"				"road_demo"
	"dz_deployInfo"			"road_demo"
}
*/

//========================================================================

mapInfoDef "vehicletrack" {
	_default_mapinfo( "vehicletrack" )

	data {
		"location"			"0.5 0.5"
		"pretty_name"		"Vehicle Track"
		"script_entrypoint"	"VehicleTrack_MapScript"
	}
}

//========================================================================
/*
mapMetaData "maps/fearog/arena" {
	"mapinfo"				"fearog_arena"
	"dz_deployInfo"			"canyon"
}
*/
mapInfoDef "fearog_arena" {
	_default_mapinfo( "fearog_arena" )

	data {
		"location"			"0.5 0.5"
		"pretty_name"		"Fearog's Testing Arena"
		"script_entrypoint"	"VehicleTrack_MapScript"
	}
}

/*
mapMetaData "maps/fearog/fearog" {
	"mapinfo"				"fearog_fearog"
	"dz_deployInfo"			"canyon"
}
*/

mapInfoDef "fearog_fearog" {
	_default_mapinfo( "fearog_fearog" )

	data {
		"location"			"0.5 0.5"
		"pretty_name"		"Fearog's Testing Arena"
		"script_entrypoint"	"VehicleTrack_MapScript"
	}
}

/*
mapMetaData "maps/fearog/for_adam" {
	"mapinfo"				"fearog_for_adam"
	"dz_deployInfo"			"canyon"
}
*/
mapInfoDef "fearog_for_adam" {
	_default_mapinfo( "fearog_for_adam" )

	data {
		"location"			"0.5 0.5"
		"pretty_name"		"Fearog's Testing Arena"
		"script_entrypoint"	"VehicleTrack_MapScript"
	}
}

/*
mapMetaData "maps/fearog/carryable_boxmap" {
	"mapinfo"				"fearog_carryable_boxmap"
	"dz_deployInfo"			"canyon"
}
*/

mapInfoDef "fearog_carryable_boxmap" {
	_default_mapinfo( "fearog_carryable_boxmap" )

	data {
		"location"			"0.5 0.5"
		"pretty_name"		"Fearog's Testing Arena"
		"script_entrypoint"	"Carryable_Box_MapScript"
	}
}

//========================================================================
// Fearog's map objective test maps
//========================================================================

//
// Ark
//
/*
mapMetaData "maps/fearog/test_ark" {
	"mapinfo"				"fearog_test_ark"
	"dz_deployInfo"			"ark"
}
*/

mapInfoDef "fearog_test_ark" {
	_default_mapinfo( "fearog_test_ark" )

	data {
		"pretty_name"			"The Ark"
		"script_entrypoint"		"Ark_MapScript"
		"mapBriefing"			"maps/ark/briefing"
		"mapLocation"			"maps/ark/location"
		"campaignDescription"	"maps/ark/campaign"
		"numObjectives"			"3"		
	}
}


//
// Island
//
/*
mapMetaData "maps/fearog/test_island" {
	"mapinfo"				"fearog_test_island"
	"dz_deployInfo"			"island"
}
*/

mapInfoDef "fearog_test_island" {
	_default_mapinfo( "fearog_test_island" )

	data {
		"pretty_name"			"Island"
		"script_entrypoint"		"Island_MapScript"
		"mapBriefing"			"maps/island/briefing"
		"mapLocation"			"maps/island/location"
		"campaignDescription"	"maps/island/campaign"
		"numObjectives"			"3"
	}
}


//
// Refinery
//

/*
mapMetaData "maps/fearog/test_refinery" {
	"mapinfo"				"fearog_test_refinery"
	"dz_deployInfo"			"refinery"
}
*/

mapInfoDef "fearog_test_refinery" {
	_default_mapinfo( "fearog_test_refinery" )
	data {
		"pretty_name"			"Refinery"
		"script_entrypoint"		"Refinery_MapScript"
		"mapBriefing"			"maps/refinery/briefing"
		"mapLocation"			"maps/refinery/location"
		"campaignDescription"	"maps/refinery/campaign"
		"numObjectives"			"4"
	}
}


//
// Quarry
//
/*
mapMetaData "maps/fearog/test_quarry" {
	"mapinfo"					"fearog_test_quarry"
	"dz_deployInfo"				"quarry"
}
*/

mapInfoDef "fearog_test_quarry" {
	_default_mapinfo( "fearog_test_quarry" )

	data {
		"pretty_name"			"Quarry"
		"script_entrypoint"		"Quarry_MapScript"
		"mapBriefing"			"maps/quarry/briefing"
		"mapLocation"			"maps/quarry/location"
		"campaignDescription"	"maps/quarry/campaign"
		"numObjectives"			"3"
	}
}


//
// Ghost Town
//

/*
mapMetaData "maps/fearog/test_ghost_town" {
	"mapinfo"				"fearog_test_ghost_town"
	"dz_deployInfo"			"ghost_town"
}
*/

mapInfoDef "fearog_test_ghost_town" {
	_default_mapinfo( "fearog_test_ghost_town" )

	data {
		"pretty_name"			"Ghost Town"
		"script_entrypoint"		"GhostTown_MapScript"
		"mapBriefing"			"maps/ghost_town/map_briefing"
		"mapLocation"			"maps/ghost_town/map_loaction"
		"campaignDescription"	"maps/ghost_town/campaign_description"
		"numObjectives"		"4"
	}
}


//
// Sewer
//

/*
mapMetaData "maps/fearog/test_sewer" {
	"mapinfo"	"fearog_test_sewer"
	"dz_deployInfo"	"sewer"
}
*/

mapInfoDef "fearog_test_sewer" {
	_default_mapinfo( "fearog_test_sewer" )

	data {
		"mapLocation"			"maps/sewer/location"
		"pretty_name"			"Sewer"
		"script_entrypoint"		"Sewer_MapScript"
		"mapBriefing"			"maps/sewer/briefing"
		"campaignDescription"	"maps/sewer/campaign"
		"numObjectives"			"3"
	}
}



//
// Area22
//
/*
mapMetaData "maps/fearog/test_area22" {
	"mapinfo"				"fearog_test_area22"
	"dz_deployInfo"			"area22"
}
*/

mapInfoDef "fearog_test_area22" {
	_default_mapinfo( "fearog_test_area22" )

	data {
		"pretty_name"			"Area 22"
		"script_entrypoint"		"Area22_MapScript"
		"mapBriefing"			"maps/area22/briefing"
		"mapLocation"			"maps/area22/location"
		"campaignDescription"	"maps/area22/campaign"
		"numObjectives"			"3"		
	}
}



//
// Valley
//
/*
mapMetaData maps/fearog/test_valley {
	"mapinfo"	"fearog_test_valley"
	"dz_deployInfo"	"valley"	
}
*/

mapInfoDef "fearog_test_valley" {
	_default_mapinfo( "fearog_test_valley" )

	data {
		"mapLocation"			"maps/valley/location"
		"pretty_name"			"Valley"
		"script_entrypoint"		"Valley_MapScript"
		"mapBriefing"			"maps/valley/briefing"
		"campaignDescription"	"maps/valley/campaign"
		"numObjectives"			"4"
	}
}

/*
mapMetaData maps/fearog/stripped_valley {
	"mapinfo"	"fearog_stripped_valley"
	"dz_deployInfo"	"valley"
	"showInBrowser"	"0"
}
*/

mapInfoDef "fearog_stripped_valley" {
	_default_mapinfo( "fearog_stripped_valley" )

	data {
		"mapLocation"			"maps/valley/location"
		"pretty_name"			"Valley"
		"script_entrypoint"		"VehicleTrack_MapScript"
		"mapBriefing"			"maps/valley/briefing"
		"campaignDescription"	"maps/valley/campaign"
		"numObjectives"			"4"
		"mtr_serverShot"		"levelshots/thumbs/valley"	
		"mtr_backdrop"			"levelshots/campaigns/northamerica"
		"mapPosition"			"311 140"
		"snd_music"				"sounds/music/load1"
		
		"megatexture1"			"megatextures/valley"
	}
}


//
// Slipgate
//

/*
mapMetaData "maps/fearog/test_slipgate" {
	"mapinfo"				"fearog_test_slipgate"
	"dz_deployInfo"			"slipgate"
}
*/

mapInfoDef "fearog_test_slipgate" {
	_default_mapinfo( "fearog_test_slipgate" )

	data {
		"pretty_name"			"Slipgate"
		"script_entrypoint"		"Slipgate_MapScript"
		"mapBriefing"			"maps/slipgate/map_briefing"
		"mapLocation"			"maps/slipgate/map_location"
		"campaignDescription"	"maps/slipgate/map_campaign_description"
		"numObjectives"			"3"		
	}
}



//========================================================================

mapInfoDef "screenshots_empty" {
	_default_mapinfo( "screenshots_empty" )

	data {
		"pretty_name"			"PRETTY!"
		"script_entrypoint"		"Empty_MapScript"
	}
}

//========================================================================
/*
mapMetaData "maps/examples/routeconstraint" {
	"mapinfo"				"routeconstraint"
	"massive_zone_name"		"vehicletrack"
}
*/
mapInfoDef "routeconstraint" {
	_default_mapinfo( "routeconstraint" )

	data {
		"location"			"0.5 0.5"
		"pretty_name"		"Route Constraint Test Map"
		"script_entrypoint"	"RouteConstraint_MapScript"
	}
}

// =================================
// TEST MAPS
// =================================
/*
mapMetaData "maps/torchy/volcano_objtest" {
	"mapinfo"				"volcano"
	"dz_deployInfo"			"volcano"
}

mapMetaData "maps/torchy/volcano" {
	"mapinfo"				"volcano"
	"dz_deployInfo"			"volcano"
}
*/