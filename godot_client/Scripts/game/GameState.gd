# GameState.gd
# Manages the current game state — hand tiles, discards, melds, scores, etc.
# Mirrors the state model in client.py

extends Node
class_name GameState

# Tile type constants (same encoding as server)
const TOTAL_TILES := 136
const NUM_TILE_TYPES := 34
const AKA_MAN5 := 16
const AKA_PIN5 := 52
const AKA_SOU5 := 88

# Tile names for display (simplified Chinese)
const TILE_NAMES := {
	0: "1m", 1: "2m", 2: "3m", 3: "4m", 4: "5m", 5: "6m", 6: "7m", 7: "8m", 8: "9m",
	9: "1p", 10: "2p", 11: "3p", 12: "4p", 13: "5p", 14: "6p", 15: "7p", 16: "8p", 17: "9p",
	18: "1s", 19: "2s", 20: "3s", 21: "4s", 22: "5s", 23: "6s", 24: "7s", 25: "8s", 26: "9s",
	27: "東", 28: "南", 29: "西", 30: "北",
	31: "白", 32: "發", 33: "中"
}

const SUIT_NAMES := ["萬", "筒", "索"]

static func tile_type(tile_id: int) -> int:
	return tile_id / 4

static func tile_number(tile_id: int) -> int:
	return (tile_id / 4) % 9

static func tile_suit(tile_id: int) -> int:
	var t = tile_id / 4
	return t / 9

static func tile_name(tile_id) -> String:
	var t = tile_id / 4
	if TILE_NAMES.has(t):
		return TILE_NAMES[t]
	return "?"

static func tile_type_name(tile_type_val: int) -> String:
	if TILE_NAMES.has(tile_type_val):
		return TILE_NAMES[tile_type_val]
	return "?"

# --- State ---
var my_seat: int = -1
var my_username: String = ""
var my_tiles: Array = []         # our hand tiles (tile IDs)
var my_furo: Dictionary = {}     # our melds
var my_machi: Array = []         # our waiting tiles (tenpai)

var game_round: int = 0
var honba: int = 0
var riichi_ba: int = 0
var oya: int = 0
var left_num: int = 0
var dora_indicators: Array = []

# All 4 agents' state
var agents: Array = []  # each element: { username, score, tile_count, discard, furo, riichi, riichi_round, river, is_ai }

var furiten: bool = false

func _init() -> void:
	reset()

func reset() -> void:
	my_seat = -1
	my_username = ""
	my_tiles.clear()
	my_furo.clear()
	my_machi.clear()
	game_round = 0
	honba = 0
	riichi_ba = 0
	oya = 0
	left_num = 0
	dora_indicators.clear()
	agents.clear()
	furiten = false

# Called when game starts
func on_game_start(game_data: Dictionary, self_info: Dictionary) -> void:
	reset()
	
	# Game info
	game_round = game_data.get("round", 0)
	honba = game_data.get("honba", 0)
	riichi_ba = game_data.get("riichi_ba", 0)
	oya = game_data.get("oya", 0)
	left_num = game_data.get("left_num", 0)
	dora_indicators = game_data.get("dora_indicator", []).duplicate()
	
	# Agent info
	agents = game_data.get("agents", []).duplicate()
	
	# Self info
	my_seat = self_info.get("seat", -1)
	my_username = self_info.get("username", "")
	my_tiles = self_info.get("tiles", []).duplicate()
	my_furo = self_info.get("furo", {}).duplicate()
	my_machi = self_info.get("machi", []).duplicate()

	my_tiles.sort()

func on_draw(who: int, tile_id: int) -> void:
	if who == my_seat:
		my_tiles.append(tile_id)
		my_tiles.sort()

func on_discard(who: int, tile_id: int, mode: int) -> void:
	if who == my_seat:
		my_tiles.erase(tile_id)
		my_tiles.sort()
	
	# Update the agent's river/discard
	if who < agents.size():
		var agent = agents[who] as Dictionary
		if not agent.has("discard"):
			agent["discard"] = []
		if not agent.has("river"):
			agent["river"] = []
		agent["discard"].append(tile_id)
		agent["river"].append(tile_id)

func on_meld(event_type: String, action: Dictionary) -> void:
	var who = action.get("who", -1)
	var pattern = action.get("pattern", [])
	var from_who = action.get("from_who", -1)
	
	if who == my_seat:
		# Remove melded tiles from hand
		var kui_tile = action.get("kui_tile", -1)
		for t in pattern:
			if t != kui_tile:
				my_tiles.erase(t)
		my_tiles.sort()
		
		# Add to furo
		var key = str(event_type, "_", my_furo.size())
		my_furo[key] = pattern

func on_riichi(action: Dictionary) -> void:
	# Handle riichi declaration
	var who = action.get("who", -1)
	if who < agents.size():
		var agent = agents[who] as Dictionary
		agent["riichi"] = 1

func on_agari(actions: Array, ura_dora: Array) -> void:
	pass

func on_ryuukyoku(data: Dictionary) -> void:
	pass

func on_settlement(data: Dictionary) -> void:
	pass

func get_agent_info(seat: int) -> Dictionary:
	if seat >= 0 and seat < agents.size():
		return agents[seat] as Dictionary
	return {}

func get_wind_name(seat: int) -> String:
	var winds = ["東家", "南家", "西家", "北家"]
	var idx = (seat - oya + 4) % 4
	return winds[idx]

func get_round_wind() -> String:
	var winds = ["東", "南", "西", "北"]
	return winds[(game_round / 4) % 4]

func get_round_number() -> int:
	return (game_round % 4) + 1
