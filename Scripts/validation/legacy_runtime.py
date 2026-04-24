from __future__ import annotations

import os
import socket
import struct
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, List, Optional, Set

from validation.transport import (
    call_client_function,
    pack_combat_unit_ref,
    pack_string,
    parse_accept_party_invite_response,
    parse_cast_skill_at_unit_response,
    parse_cast_skill_response,
    parse_change_gold_response,
    parse_create_party_response,
    parse_debug_spawn_monster_response,
    parse_equip_item_response,
    parse_find_player_response,
    parse_grant_experience_response,
    parse_invite_party_response,
    parse_kick_party_member_response,
    parse_login_response,
    parse_logout_response,
    parse_modify_health_response,
    parse_party_created_notify,
    parse_party_invite_received_notify,
    parse_party_member_joined_notify,
    parse_party_member_removed_notify,
    parse_pawn_state_response,
    parse_query_combat_profile_response,
    parse_query_inventory_response,
    parse_query_profile_response,
    parse_query_progression_response,
    parse_scene_leave_message,
    parse_scene_state_message,
    parse_set_primary_skill_response,
    parse_switch_scene_response,
    recv_client_downlink,
)

ROUTER_PORT = 8005
GATEWAY_PORT = 8001
LOGIN_PORT = 8002
WORLD_PORT = 8003
SCENE_PORT = 8004
MGO_PORT = 8006


@dataclass(frozen=True)
class LegacyValidationRuntimeHooks:
    log: Callable[[str], None]
    stop_lingering_servers: Callable[[], None]
    start_server: Callable[[Path, str, Optional[dict], Path], Optional[subprocess.Popen]]
    wait_for_port: Callable[[str, int, float], bool]


def wait_until(deadline: float, step: Callable[[], Optional[dict]]) -> Optional[dict]:
    while time.time() < deadline:
        value = step()
        if value is not None:
            return value
        time.sleep(0.2)
    return None


def try_login(sock: socket.socket, player_id: int) -> Optional[dict]:
    try:
        payload = call_client_function(sock, "Client_Login", struct.pack("<Q", player_id), timeout=3.0)
        return parse_login_response(payload)
    except (TimeoutError, ValueError, OSError):
        return None


def try_find_player(sock: socket.socket, payload: bytes, timeout: float = 3.0) -> Optional[dict]:
    try:
        return parse_find_player_response(call_client_function(sock, "Client_FindPlayer", payload, timeout=timeout))
    except (TimeoutError, ValueError, OSError):
        return None



class LegacyValidationRuntime:
    def __init__(self, hooks: LegacyValidationRuntimeHooks):
        self.hooks = hooks

    def run_validation(
        self,
        build_dir: Path,
        timeout: float,
        zone_id: Optional[int],
        log_dir: Path,
        enable_mgo: bool,
        mongo_db: str,
        mongo_collection: str,
        enabled_tests: Set[int],
    ) -> bool:
        log = self.hooks.log
        stop_lingering_servers = self.hooks.stop_lingering_servers
        start_server = self.hooks.start_server
        wait_for_port = self.hooks.wait_for_port

        procs: List[subprocess.Popen] = []
        proc_by_name: dict[str, subprocess.Popen] = {}
        stop_lingering_servers()
        log_dir.mkdir(parents=True, exist_ok=True)

        base_env = {}
        if zone_id is not None:
            base_env["MESSION_ZONE_ID"] = str(zone_id)

        if enable_mgo:
            base_env["MESSION_WORLD_MGO_PERSISTENCE_ENABLE"] = "1"
            base_env["MESSION_MGO_MONGO_ENABLE"] = os.environ.get("MESSION_MGO_MONGO_ENABLE", "1")
            base_env["MESSION_MGO_MONGO_DB"] = os.environ.get("MESSION_MGO_MONGO_DB", mongo_db)
            base_env["MESSION_MGO_MONGO_COLLECTION"] = os.environ.get(
                "MESSION_MGO_MONGO_COLLECTION",
                mongo_collection,
            )
            log(
                "Mongo sandbox target: "
                f"db={base_env['MESSION_MGO_MONGO_DB']} "
                f"collection={base_env['MESSION_MGO_MONGO_COLLECTION']}"
            )

        def cleanup() -> None:
            for proc in reversed(procs):
                try:
                    proc.terminate()
                    proc.wait(timeout=3)
                except Exception:
                    try:
                        proc.kill()
                    except Exception:
                        pass

        def launch(name: str, port: int) -> bool:
            proc = start_server(build_dir, name, dict(base_env), log_dir)
            if proc is None:
                return False
            procs.append(proc)
            proc_by_name[name] = proc
            if not wait_for_port("127.0.0.1", port, timeout):
                log(f"{name} did not become ready on port {port}")
                return False
            return True

        try:
            if not launch("RouterServer", ROUTER_PORT):
                return False
            if enable_mgo and not launch("MgoServer", MGO_PORT):
                return False
            if not launch("LoginServer", LOGIN_PORT):
                return False
            if not launch("WorldServer", WORLD_PORT):
                return False
            if not launch("SceneServer", SCENE_PORT):
                return False
            if not launch("GatewayServer", GATEWAY_PORT):
                return False

            log(f"Server logs: {log_dir}")
            time.sleep(2.0)

            player_id = int(time.time() * 1000) % 4000000000 + 10001
            other_player_id = player_id + 1
            next_scene_id = 2
            initial_session_key = 0
            party_id = 0

            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(5.0)
            sock2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock2.settimeout(5.0)
            try:
                sock.connect(("127.0.0.1", GATEWAY_PORT))
                sock2.connect(("127.0.0.1", GATEWAY_PORT))

                if 1 in enabled_tests:
                    log("Test 1: Client_Login...")
                    login_deadline = time.time() + max(timeout, 10.0)
                    login_response = wait_until(login_deadline, lambda: try_login(sock, player_id))
                    if login_response is None:
                        log("  Client_Login failed: no valid response before timeout")
                        return False
                    if not login_response["bSuccess"]:
                        log(f"  Client_Login failed: error={login_response['Error']}")
                        return False
                    if login_response["PlayerId"] != player_id or login_response["SessionKey"] == 0:
                        log(
                            "  Client_Login returned unexpected payload: "
                            f"playerId={login_response['PlayerId']} sessionKey={login_response['SessionKey']}"
                        )
                        return False
                    initial_session_key = login_response["SessionKey"]
                    log(
                        "  Client_Login OK: "
                        f"playerId={login_response['PlayerId']} sessionKey={login_response['SessionKey']}"
                    )

                if 2 in enabled_tests:
                    log("Test 2: Client_FindPlayer after login...")
                    find_response = parse_find_player_response(
                        call_client_function(sock, "Client_FindPlayer", struct.pack("<Q", player_id))
                    )
                    if not find_response["bFound"]:
                        log(f"  Client_FindPlayer failed: error={find_response['Error']}")
                        return False
                    if find_response["PlayerId"] != player_id or find_response["GatewayConnectionId"] == 0:
                        log(
                            "  Client_FindPlayer returned unexpected payload: "
                            f"playerId={find_response['PlayerId']} "
                            f"gatewayConnectionId={find_response['GatewayConnectionId']}"
                        )
                        return False
                    log(
                        "  Client_FindPlayer OK: "
                        f"playerId={find_response['PlayerId']} "
                        f"gatewayConnectionId={find_response['GatewayConnectionId']} "
                        f"sceneId={find_response['SceneId']}"
                    )

                if 3 in enabled_tests:
                    log("Test 3: Client_SwitchScene...")
                    switch_payload = struct.pack("<QI", player_id, next_scene_id)
                    switch_response = parse_switch_scene_response(
                        call_client_function(sock, "Client_SwitchScene", switch_payload)
                    )
                    if not switch_response["bSuccess"]:
                        log(f"  Client_SwitchScene failed: error={switch_response['Error']}")
                        return False
                    if switch_response["PlayerId"] != player_id or switch_response["SceneId"] != next_scene_id:
                        log(
                            "  Client_SwitchScene returned unexpected payload: "
                            f"playerId={switch_response['PlayerId']} sceneId={switch_response['SceneId']}"
                        )
                        return False
                    log(
                        "  Client_SwitchScene OK: "
                        f"playerId={switch_response['PlayerId']} sceneId={switch_response['SceneId']}"
                    )

                if 4 in enabled_tests:
                    log("Test 4: Client_FindPlayer after switch...")
                    find_after_switch = parse_find_player_response(
                        call_client_function(sock, "Client_FindPlayer", struct.pack("<Q", player_id))
                    )
                    if not find_after_switch["bFound"] or find_after_switch["SceneId"] != next_scene_id:
                        log(
                            "  Client_FindPlayer after switch returned unexpected payload: "
                            f"found={find_after_switch['bFound']} sceneId={find_after_switch['SceneId']} "
                            f"error={find_after_switch['Error']}"
                        )
                        return False
                    log(
                        "  Client_FindPlayer after switch OK: "
                        f"playerId={find_after_switch['PlayerId']} sceneId={find_after_switch['SceneId']}"
                    )

                if 5 in enabled_tests:
                    log("Test 5: Client_Move...")
                    move_payload = struct.pack("<Qfff", player_id, 128.5, 64.25, 7.0)
                    move_response = parse_pawn_state_response(
                        call_client_function(sock, "Client_Move", move_payload)
                    )
                    if not move_response["bSuccess"]:
                        log(f"  Client_Move failed: error={move_response['Error']}")
                        return False
                    if (
                        move_response["PlayerId"] != player_id
                        or move_response["SceneId"] != next_scene_id
                        or abs(move_response["X"] - 128.5) > 0.001
                        or abs(move_response["Y"] - 64.25) > 0.001
                        or abs(move_response["Z"] - 7.0) > 0.001
                        or move_response["Health"] != 100
                    ):
                        log(f"  Client_Move returned unexpected payload: {move_response}")
                        return False
                    log(f"  Client_Move OK: {move_response}")

                if 6 in enabled_tests:
                    log("Test 6: Client_QueryPawn...")
                    query_pawn_response = parse_pawn_state_response(
                        call_client_function(sock, "Client_QueryPawn", struct.pack("<Q", player_id))
                    )
                    if (
                        not query_pawn_response["bSuccess"]
                        or query_pawn_response["PlayerId"] != player_id
                        or query_pawn_response["SceneId"] != next_scene_id
                        or abs(query_pawn_response["X"] - 128.5) > 0.001
                        or abs(query_pawn_response["Y"] - 64.25) > 0.001
                        or abs(query_pawn_response["Z"] - 7.0) > 0.001
                        or query_pawn_response["Health"] != 100
                    ):
                        log(f"  Client_QueryPawn returned unexpected payload: {query_pawn_response}")
                        return False
                    log(f"  Client_QueryPawn OK: {query_pawn_response}")

                if 7 in enabled_tests:
                    log("Test 7: Client_ChangeGold...")
                    change_gold_response = parse_change_gold_response(
                        call_client_function(sock, "Client_ChangeGold", struct.pack("<Qi", player_id, 50))
                    )
                    if not change_gold_response["bSuccess"]:
                        log(f"  Client_ChangeGold failed: error={change_gold_response['Error']}")
                        return False
                    if change_gold_response["PlayerId"] != player_id or change_gold_response["Gold"] != 50:
                        log(
                            "  Client_ChangeGold returned unexpected payload: "
                            f"playerId={change_gold_response['PlayerId']} gold={change_gold_response['Gold']}"
                        )
                        return False
                    log(
                        "  Client_ChangeGold OK: "
                        f"playerId={change_gold_response['PlayerId']} gold={change_gold_response['Gold']}"
                    )

                if 8 in enabled_tests:
                    log("Test 8: Client_EquipItem...")
                    equip_item_response = parse_equip_item_response(
                        call_client_function(
                            sock,
                            "Client_EquipItem",
                            struct.pack("<Q", player_id) + pack_string("iron_sword"),
                        )
                    )
                    if not equip_item_response["bSuccess"]:
                        log(f"  Client_EquipItem failed: error={equip_item_response['Error']}")
                        return False
                    if equip_item_response["PlayerId"] != player_id or equip_item_response["EquippedItem"] != "iron_sword":
                        log(
                            "  Client_EquipItem returned unexpected payload: "
                            f"playerId={equip_item_response['PlayerId']} "
                            f"equippedItem={equip_item_response['EquippedItem']}"
                        )
                        return False
                    log(
                        "  Client_EquipItem OK: "
                        f"playerId={equip_item_response['PlayerId']} "
                        f"equippedItem={equip_item_response['EquippedItem']}"
                    )

                if 9 in enabled_tests:
                    log("Test 9: Client_GrantExperience...")
                    grant_experience_response = parse_grant_experience_response(
                        call_client_function(sock, "Client_GrantExperience", struct.pack("<QI", player_id, 250))
                    )
                    if not grant_experience_response["bSuccess"]:
                        log(f"  Client_GrantExperience failed: error={grant_experience_response['Error']}")
                        return False
                    if (
                        grant_experience_response["PlayerId"] != player_id
                        or grant_experience_response["Level"] != 3
                        or grant_experience_response["Experience"] != 250
                    ):
                        log(
                            "  Client_GrantExperience returned unexpected payload: "
                            f"playerId={grant_experience_response['PlayerId']} "
                            f"level={grant_experience_response['Level']} "
                            f"experience={grant_experience_response['Experience']}"
                        )
                        return False
                    log(
                        "  Client_GrantExperience OK: "
                        f"playerId={grant_experience_response['PlayerId']} "
                        f"level={grant_experience_response['Level']} "
                        f"experience={grant_experience_response['Experience']}"
                    )

                if 10 in enabled_tests:
                    log("Test 10: Client_ModifyHealth...")
                    modify_health_response = parse_modify_health_response(
                        call_client_function(sock, "Client_ModifyHealth", struct.pack("<Qi", player_id, -25))
                    )
                    if not modify_health_response["bSuccess"]:
                        log(f"  Client_ModifyHealth failed: error={modify_health_response['Error']}")
                        return False
                    if modify_health_response["PlayerId"] != player_id or modify_health_response["Health"] != 75:
                        log(
                            "  Client_ModifyHealth returned unexpected payload: "
                            f"playerId={modify_health_response['PlayerId']} health={modify_health_response['Health']}"
                        )
                        return False
                    log(
                        "  Client_ModifyHealth OK: "
                        f"playerId={modify_health_response['PlayerId']} health={modify_health_response['Health']}"
                    )

                if 11 in enabled_tests:
                    log("Test 11: Client_QueryPawn after health change...")
                    query_pawn_after_health = parse_pawn_state_response(
                        call_client_function(sock, "Client_QueryPawn", struct.pack("<Q", player_id))
                    )
                    if (
                        not query_pawn_after_health["bSuccess"]
                        or query_pawn_after_health["PlayerId"] != player_id
                        or query_pawn_after_health["SceneId"] != next_scene_id
                        or abs(query_pawn_after_health["X"] - 128.5) > 0.001
                        or abs(query_pawn_after_health["Y"] - 64.25) > 0.001
                        or abs(query_pawn_after_health["Z"] - 7.0) > 0.001
                        or query_pawn_after_health["Health"] != 75
                    ):
                        log(
                            "  Client_QueryPawn after health change returned unexpected payload: "
                            f"{query_pawn_after_health}"
                        )
                        return False
                    log(f"  Client_QueryPawn after health change OK: {query_pawn_after_health}")

                if 12 in enabled_tests:
                    log("Test 12: Client_QueryProfile after writes...")
                    query_profile_response = parse_query_profile_response(
                        call_client_function(sock, "Client_QueryProfile", struct.pack("<Q", player_id))
                    )
                    if (
                        not query_profile_response["bSuccess"]
                        or query_profile_response["PlayerId"] != player_id
                        or query_profile_response["CurrentSceneId"] != next_scene_id
                        or query_profile_response["Gold"] != 50
                        or query_profile_response["EquippedItem"] != "iron_sword"
                        or query_profile_response["Level"] != 3
                        or query_profile_response["Experience"] != 250
                        or query_profile_response["Health"] != 75
                    ):
                        log(
                            "  Client_QueryProfile returned unexpected payload: "
                            f"{query_profile_response}"
                        )
                        return False
                    log(f"  Client_QueryProfile OK: {query_profile_response}")

                if 13 in enabled_tests:
                    log("Test 13: Client_QueryInventory after writes...")
                    query_inventory_response = parse_query_inventory_response(
                        call_client_function(sock, "Client_QueryInventory", struct.pack("<Q", player_id))
                    )
                    if (
                        not query_inventory_response["bSuccess"]
                        or query_inventory_response["PlayerId"] != player_id
                        or query_inventory_response["Gold"] != 50
                        or query_inventory_response["EquippedItem"] != "iron_sword"
                    ):
                        log(
                            "  Client_QueryInventory returned unexpected payload: "
                            f"{query_inventory_response}"
                        )
                        return False
                    log(f"  Client_QueryInventory OK: {query_inventory_response}")

                if 14 in enabled_tests:
                    log("Test 14: Client_QueryProgression after writes...")
                    query_progression_response = parse_query_progression_response(
                        call_client_function(sock, "Client_QueryProgression", struct.pack("<Q", player_id))
                    )
                    if (
                        not query_progression_response["bSuccess"]
                        or query_progression_response["PlayerId"] != player_id
                        or query_progression_response["Level"] != 3
                        or query_progression_response["Experience"] != 250
                        or query_progression_response["Health"] != 75
                    ):
                        log(
                            "  Client_QueryProgression returned unexpected payload: "
                            f"{query_progression_response}"
                        )
                        return False
                    log(f"  Client_QueryProgression OK: {query_progression_response}")

                if 31 in enabled_tests:
                    log("Test 31: Client_SetPrimarySkill...")
                    set_primary_skill_response = parse_set_primary_skill_response(
                        call_client_function(sock, "Client_SetPrimarySkill", struct.pack("<QI", player_id, 2002))
                    )
                    if not set_primary_skill_response["bSuccess"]:
                        log(f"  Client_SetPrimarySkill failed: error={set_primary_skill_response['Error']}")
                        return False
                    if (
                        set_primary_skill_response["PlayerId"] != player_id
                        or set_primary_skill_response["PrimarySkillId"] != 2002
                    ):
                        log(
                            "  Client_SetPrimarySkill returned unexpected payload: "
                            f"{set_primary_skill_response}"
                        )
                        return False
                    log(f"  Client_SetPrimarySkill OK: {set_primary_skill_response}")

                if 32 in enabled_tests:
                    log("Test 32: Client_QueryCombatProfile after writes...")
                    query_combat_profile_after_write = parse_query_combat_profile_response(
                        call_client_function(sock, "Client_QueryCombatProfile", struct.pack("<Q", player_id))
                    )
                    if (
                        not query_combat_profile_after_write["bSuccess"]
                        or query_combat_profile_after_write["PlayerId"] != player_id
                        or query_combat_profile_after_write["BaseAttack"] != 10
                        or query_combat_profile_after_write["BaseDefense"] != 5
                        or query_combat_profile_after_write["MaxHealth"] != 100
                        or query_combat_profile_after_write["PrimarySkillId"] != 2002
                        or query_combat_profile_after_write["LastResolvedSceneId"] != 0
                        or query_combat_profile_after_write["LastResolvedHealth"] != 100
                    ):
                        log(
                            "  Client_QueryCombatProfile after writes returned unexpected payload: "
                            f"{query_combat_profile_after_write}"
                        )
                        return False
                    log(f"  Client_QueryCombatProfile after writes OK: {query_combat_profile_after_write}")

                if 15 in enabled_tests:
                    log("Test 15: second player enter same scene and trigger downlink...")
                    other_login_response = wait_until(
                        time.time() + max(timeout, 10.0),
                        lambda: try_login(sock2, other_player_id),
                    )
                    if other_login_response is None:
                        log("  second player login failed: no valid response before timeout")
                        return False
                    if not other_login_response["bSuccess"]:
                        log(f"  second player login failed: error={other_login_response['Error']}")
                        return False

                    other_switch_response = parse_switch_scene_response(
                        call_client_function(sock2, "Client_SwitchScene", struct.pack("<QI", other_player_id, next_scene_id))
                    )
                    if not other_switch_response["bSuccess"]:
                        log(f"  second player switch failed: error={other_switch_response['Error']}")
                        return False

                    enter_downlink = parse_scene_state_message(
                        recv_client_downlink(sock, "Client_ScenePlayerEnter", 5.0)
                    )
                    if (
                        enter_downlink["PlayerId"] != other_player_id
                        or enter_downlink["SceneId"] != next_scene_id
                        or abs(enter_downlink["X"]) > 0.001
                        or abs(enter_downlink["Y"]) > 0.001
                        or abs(enter_downlink["Z"]) > 0.001
                    ):
                        log(f"  Client_ScenePlayerEnter returned unexpected payload: {enter_downlink}")
                        return False
                    log(f"  Client_ScenePlayerEnter OK: {enter_downlink}")

                if 34 in enabled_tests:
                    log("Test 34: Client_CreateParty...")
                    create_party_response = parse_create_party_response(
                        call_client_function(sock, "Client_CreateParty", struct.pack("<Q", player_id))
                    )
                    if not create_party_response["bSuccess"]:
                        log(f"  Client_CreateParty failed: error={create_party_response['Error']}")
                        return False
                    if (
                        create_party_response["PlayerId"] != player_id
                        or create_party_response["LeaderPlayerId"] != player_id
                        or create_party_response["MemberCount"] != 1
                        or create_party_response["PartyId"] == 0
                    ):
                        log(f"  Client_CreateParty returned unexpected payload: {create_party_response}")
                        return False
                    party_id = create_party_response["PartyId"]
                    log(f"  Client_CreateParty OK: {create_party_response}")

                if 35 in enabled_tests:
                    log("Test 35: Client_InviteParty...")
                    invite_party_response = parse_invite_party_response(
                        call_client_function(sock, "Client_InviteParty", struct.pack("<QQ", player_id, other_player_id))
                    )
                    if not invite_party_response["bSuccess"]:
                        log(f"  Client_InviteParty failed: error={invite_party_response['Error']}")
                        return False
                    if (
                        invite_party_response["PlayerId"] != player_id
                        or invite_party_response["PartyId"] != party_id
                        or invite_party_response["TargetPlayerId"] != other_player_id
                    ):
                        log(f"  Client_InviteParty returned unexpected payload: {invite_party_response}")
                        return False
                    party_invite_notify = parse_party_invite_received_notify(
                        recv_client_downlink(sock2, "Client_PartyInviteReceived", 5.0)
                    )
                    if (
                        party_invite_notify["PartyId"] != party_id
                        or party_invite_notify["LeaderPlayerId"] != player_id
                        or party_invite_notify["TargetPlayerId"] != other_player_id
                    ):
                        log(f"  Client_PartyInviteReceived returned unexpected payload: {party_invite_notify}")
                        return False
                    log(f"  Client_InviteParty OK: {invite_party_response}")

                if 36 in enabled_tests:
                    log("Test 36: Client_AcceptPartyInvite...")
                    accept_party_invite_response = parse_accept_party_invite_response(
                        call_client_function(sock2, "Client_AcceptPartyInvite", struct.pack("<QQ", other_player_id, party_id))
                    )
                    if not accept_party_invite_response["bSuccess"]:
                        log(f"  Client_AcceptPartyInvite failed: error={accept_party_invite_response['Error']}")
                        return False
                    if (
                        accept_party_invite_response["PlayerId"] != other_player_id
                        or accept_party_invite_response["PartyId"] != party_id
                        or accept_party_invite_response["LeaderPlayerId"] != player_id
                        or accept_party_invite_response["MemberCount"] != 2
                    ):
                        log(
                            "  Client_AcceptPartyInvite returned unexpected payload: "
                            f"{accept_party_invite_response}"
                        )
                        return False
                    party_member_joined_notify = parse_party_member_joined_notify(
                        recv_client_downlink(sock, "Client_PartyMemberJoined", 5.0)
                    )
                    if (
                        party_member_joined_notify["PartyId"] != party_id
                        or party_member_joined_notify["LeaderPlayerId"] != player_id
                        or party_member_joined_notify["JoinedPlayerId"] != other_player_id
                        or party_member_joined_notify["MemberPlayerIds"] != [player_id, other_player_id]
                    ):
                        log(f"  Client_PartyMemberJoined returned unexpected payload: {party_member_joined_notify}")
                        return False
                    log(f"  Client_AcceptPartyInvite OK: {accept_party_invite_response}")

                if 37 in enabled_tests:
                    log("Test 37: Client_KickPartyMember...")
                    kick_party_member_response = parse_kick_party_member_response(
                        call_client_function(sock, "Client_KickPartyMember", struct.pack("<QQQ", player_id, party_id, other_player_id))
                    )
                    if not kick_party_member_response["bSuccess"]:
                        log(f"  Client_KickPartyMember failed: error={kick_party_member_response['Error']}")
                        return False
                    if (
                        kick_party_member_response["PlayerId"] != player_id
                        or kick_party_member_response["PartyId"] != party_id
                        or kick_party_member_response["TargetPlayerId"] != other_player_id
                        or kick_party_member_response["MemberCount"] != 1
                    ):
                        log(
                            "  Client_KickPartyMember returned unexpected payload: "
                            f"{kick_party_member_response}"
                        )
                        return False
                    party_member_removed_notify = parse_party_member_removed_notify(
                        recv_client_downlink(sock2, "Client_PartyMemberRemoved", 5.0)
                    )
                    if (
                        party_member_removed_notify["PartyId"] != party_id
                        or party_member_removed_notify["LeaderPlayerId"] != player_id
                        or party_member_removed_notify["RemovedPlayerId"] != other_player_id
                        or party_member_removed_notify["MemberPlayerIds"] != [player_id]
                        or party_member_removed_notify["Reason"] != "member_kicked"
                    ):
                        log(f"  Client_PartyMemberRemoved returned unexpected payload: {party_member_removed_notify}")
                        return False
                    log(f"  Client_KickPartyMember OK: {kick_party_member_response}")

                if 16 in enabled_tests:
                    log("Test 16: Client_CastSkill...")
                    cast_skill_response = parse_cast_skill_response(
                        call_client_function(sock, "Client_CastSkill", struct.pack("<QQI", player_id, other_player_id, 1001))
                    )
                    if not cast_skill_response["bSuccess"]:
                        log(f"  Client_CastSkill failed: error={cast_skill_response['Error']}")
                        return False
                    if (
                        cast_skill_response["PlayerId"] != player_id
                        or cast_skill_response["TargetPlayerId"] != other_player_id
                        or cast_skill_response["SkillId"] != 1001
                        or cast_skill_response["SceneId"] != next_scene_id
                        or cast_skill_response["AppliedDamage"] != 5
                        or cast_skill_response["TargetHealth"] != 95
                    ):
                        log(f"  Client_CastSkill returned unexpected payload: {cast_skill_response}")
                        return False
                    log(f"  Client_CastSkill OK: {cast_skill_response}")

                if 17 in enabled_tests:
                    log("Test 17: target Client_QueryPawn after skill...")
                    target_pawn_after_skill = parse_pawn_state_response(
                        call_client_function(sock2, "Client_QueryPawn", struct.pack("<Q", other_player_id))
                    )
                    if (
                        not target_pawn_after_skill["bSuccess"]
                        or target_pawn_after_skill["PlayerId"] != other_player_id
                        or target_pawn_after_skill["SceneId"] != next_scene_id
                        or target_pawn_after_skill["Health"] != 95
                    ):
                        log(f"  target Client_QueryPawn after skill returned unexpected payload: {target_pawn_after_skill}")
                        return False
                    log(f"  target Client_QueryPawn after skill OK: {target_pawn_after_skill}")

                if 30 in enabled_tests:
                    log("Test 30: target Client_QueryCombatProfile after skill...")
                    target_combat_profile_after_skill = parse_query_combat_profile_response(
                        call_client_function(sock2, "Client_QueryCombatProfile", struct.pack("<Q", other_player_id))
                    )
                    if (
                        not target_combat_profile_after_skill["bSuccess"]
                        or target_combat_profile_after_skill["PlayerId"] != other_player_id
                        or target_combat_profile_after_skill["BaseAttack"] != 10
                        or target_combat_profile_after_skill["BaseDefense"] != 5
                        or target_combat_profile_after_skill["MaxHealth"] != 100
                        or target_combat_profile_after_skill["PrimarySkillId"] != 1001
                        or target_combat_profile_after_skill["LastResolvedSceneId"] != next_scene_id
                        or target_combat_profile_after_skill["LastResolvedHealth"] != 95
                    ):
                        log(
                            "  target Client_QueryCombatProfile after skill returned unexpected payload: "
                            f"{target_combat_profile_after_skill}"
                        )
                        return False
                    log(f"  target Client_QueryCombatProfile after skill OK: {target_combat_profile_after_skill}")

                if 38 in enabled_tests:
                    log("Test 38: target Client_QueryProfile after skill...")
                    target_profile_after_skill = parse_query_profile_response(
                        call_client_function(sock2, "Client_QueryProfile", struct.pack("<Q", other_player_id))
                    )
                    if (
                        not target_profile_after_skill["bSuccess"]
                        or target_profile_after_skill["PlayerId"] != other_player_id
                        or target_profile_after_skill["CurrentSceneId"] != next_scene_id
                        or target_profile_after_skill["Level"] != 1
                        or target_profile_after_skill["Experience"] != 0
                        or target_profile_after_skill["Health"] != 95
                    ):
                        log(
                            "  target Client_QueryProfile after skill returned unexpected payload: "
                            f"{target_profile_after_skill}"
                        )
                        return False
                    log(f"  target Client_QueryProfile after skill OK: {target_profile_after_skill}")

                if 39 in enabled_tests:
                    log("Test 39: target Client_QueryProgression after skill...")
                    target_progression_after_skill = parse_query_progression_response(
                        call_client_function(sock2, "Client_QueryProgression", struct.pack("<Q", other_player_id))
                    )
                    if (
                        not target_progression_after_skill["bSuccess"]
                        or target_progression_after_skill["PlayerId"] != other_player_id
                        or target_progression_after_skill["Level"] != 1
                        or target_progression_after_skill["Experience"] != 0
                        or target_progression_after_skill["Health"] != 95
                    ):
                        log(
                            "  target Client_QueryProgression after skill returned unexpected payload: "
                            f"{target_progression_after_skill}"
                        )
                        return False
                    log(f"  target Client_QueryProgression after skill OK: {target_progression_after_skill}")

                spawned_monster_unit = None
                if 40 in enabled_tests:
                    log("Test 40: Client_DebugSpawnMonster...")
                    spawn_monster_payload = (
                        struct.pack("<QI", player_id, 9001)
                        + pack_string("validate_slime")
                        + struct.pack("<IIIIIII", 50, 50, 3, 0, 1001, 120, 25)
                    )
                    spawn_monster_response = parse_debug_spawn_monster_response(
                        call_client_function(sock, "Client_DebugSpawnMonster", spawn_monster_payload)
                    )
                    if not spawn_monster_response["bSuccess"]:
                        log(f"  Client_DebugSpawnMonster failed: error={spawn_monster_response['Error']}")
                        return False
                    monster_unit = spawn_monster_response["MonsterUnit"]
                    if (
                        spawn_monster_response["PlayerId"] != player_id
                        or spawn_monster_response["SceneId"] != next_scene_id
                        or spawn_monster_response["MonsterTemplateId"] != 9001
                        or monster_unit["UnitKind"] != 2
                        or monster_unit["CombatEntityId"] == 0
                        or monster_unit["PlayerId"] != 0
                    ):
                        log(f"  Client_DebugSpawnMonster returned unexpected payload: {spawn_monster_response}")
                        return False
                    spawned_monster_unit = monster_unit
                    log(f"  Client_DebugSpawnMonster OK: {spawn_monster_response}")

                if 41 in enabled_tests:
                    log("Test 41: Client_CastSkillAtUnit on monster...")
                    if spawned_monster_unit is None:
                        log("  Client_CastSkillAtUnit skipped: monster was not spawned")
                        return False
                    cast_skill_at_unit_payload = (
                        struct.pack("<Q", player_id)
                        + pack_combat_unit_ref(
                            spawned_monster_unit["UnitKind"],
                            spawned_monster_unit["CombatEntityId"],
                            spawned_monster_unit["PlayerId"],
                        )
                        + struct.pack("<I", 1001)
                    )
                    cast_skill_at_unit_response = parse_cast_skill_at_unit_response(
                        call_client_function(sock, "Client_CastSkillAtUnit", cast_skill_at_unit_payload)
                    )
                    if not cast_skill_at_unit_response["bSuccess"]:
                        log(f"  Client_CastSkillAtUnit failed: error={cast_skill_at_unit_response['Error']}")
                        return False
                    if (
                        cast_skill_at_unit_response["PlayerId"] != player_id
                        or cast_skill_at_unit_response["TargetUnit"]["UnitKind"] != 2
                        or cast_skill_at_unit_response["TargetUnit"]["CombatEntityId"] != spawned_monster_unit["CombatEntityId"]
                        or cast_skill_at_unit_response["SkillId"] != 1001
                        or cast_skill_at_unit_response["SceneId"] != next_scene_id
                        or cast_skill_at_unit_response["AppliedDamage"] != 10
                        or cast_skill_at_unit_response["TargetHealth"] != 40
                        or cast_skill_at_unit_response["bTargetDefeated"]
                        or cast_skill_at_unit_response["ExperienceReward"] != 0
                        or cast_skill_at_unit_response["GoldReward"] != 0
                    ):
                        log(f"  Client_CastSkillAtUnit returned unexpected payload: {cast_skill_at_unit_response}")
                        return False
                    log(f"  Client_CastSkillAtUnit OK: {cast_skill_at_unit_response}")

                if 42 in enabled_tests:
                    log("Test 42: kill monster and apply rewards...")
                    if spawned_monster_unit is None:
                        log("  monster kill test skipped: monster was not spawned")
                        return False

                    final_monster_kill_response = None
                    expected_healths = [30, 20, 10, 0]
                    for cast_index, expected_target_health in enumerate(expected_healths, start=1):
                        cast_skill_at_unit_payload = (
                            struct.pack("<Q", player_id)
                            + pack_combat_unit_ref(
                                spawned_monster_unit["UnitKind"],
                                spawned_monster_unit["CombatEntityId"],
                                spawned_monster_unit["PlayerId"],
                            )
                            + struct.pack("<I", 1001)
                        )
                        cast_skill_at_unit_response = parse_cast_skill_at_unit_response(
                            call_client_function(sock, "Client_CastSkillAtUnit", cast_skill_at_unit_payload)
                        )
                        if not cast_skill_at_unit_response["bSuccess"]:
                            log(
                                "  monster kill cast failed on step "
                                f"{cast_index}: error={cast_skill_at_unit_response['Error']}"
                            )
                            return False
                        if (
                            cast_skill_at_unit_response["PlayerId"] != player_id
                            or cast_skill_at_unit_response["TargetUnit"]["UnitKind"] != 2
                            or cast_skill_at_unit_response["TargetUnit"]["CombatEntityId"] != spawned_monster_unit["CombatEntityId"]
                            or cast_skill_at_unit_response["SkillId"] != 1001
                            or cast_skill_at_unit_response["SceneId"] != next_scene_id
                            or cast_skill_at_unit_response["AppliedDamage"] != 10
                            or cast_skill_at_unit_response["TargetHealth"] != expected_target_health
                        ):
                            log(
                                "  monster kill cast returned unexpected payload on step "
                                f"{cast_index}: {cast_skill_at_unit_response}"
                            )
                            return False
                        if expected_target_health != 0:
                            if (
                                cast_skill_at_unit_response["bTargetDefeated"]
                                or cast_skill_at_unit_response["ExperienceReward"] != 0
                                or cast_skill_at_unit_response["GoldReward"] != 0
                            ):
                                log(
                                    "  monster kill cast returned premature reward payload on step "
                                    f"{cast_index}: {cast_skill_at_unit_response}"
                                )
                                return False
                        else:
                            if (
                                not cast_skill_at_unit_response["bTargetDefeated"]
                                or cast_skill_at_unit_response["ExperienceReward"] != 120
                                or cast_skill_at_unit_response["GoldReward"] != 25
                            ):
                                log(f"  monster final kill payload unexpected: {cast_skill_at_unit_response}")
                                return False
                            final_monster_kill_response = cast_skill_at_unit_response

                    if final_monster_kill_response is None:
                        log("  monster final kill response missing")
                        return False

                    caster_profile_after_kill = parse_query_profile_response(
                        call_client_function(sock, "Client_QueryProfile", struct.pack("<Q", player_id))
                    )
                    if (
                        not caster_profile_after_kill["bSuccess"]
                        or caster_profile_after_kill["PlayerId"] != player_id
                        or caster_profile_after_kill["CurrentSceneId"] != next_scene_id
                        or caster_profile_after_kill["Gold"] != 25
                        or caster_profile_after_kill["Level"] != 2
                        or caster_profile_after_kill["Experience"] != 120
                        or caster_profile_after_kill["Health"] != 100
                    ):
                        log(f"  Client_QueryProfile after monster kill returned unexpected payload: {caster_profile_after_kill}")
                        return False
                    log(f"  Client_QueryProfile after monster kill OK: {caster_profile_after_kill}")

                    caster_inventory_after_kill = parse_query_inventory_response(
                        call_client_function(sock, "Client_QueryInventory", struct.pack("<Q", player_id))
                    )
                    if (
                        not caster_inventory_after_kill["bSuccess"]
                        or caster_inventory_after_kill["PlayerId"] != player_id
                        or caster_inventory_after_kill["Gold"] != 25
                        or caster_inventory_after_kill["EquippedItem"] != "starter_sword"
                    ):
                        log(
                            "  Client_QueryInventory after monster kill returned unexpected payload: "
                            f"{caster_inventory_after_kill}"
                        )
                        return False
                    log(f"  Client_QueryInventory after monster kill OK: {caster_inventory_after_kill}")

                    caster_progression_after_kill = parse_query_progression_response(
                        call_client_function(sock, "Client_QueryProgression", struct.pack("<Q", player_id))
                    )
                    if (
                        not caster_progression_after_kill["bSuccess"]
                        or caster_progression_after_kill["PlayerId"] != player_id
                        or caster_progression_after_kill["Level"] != 2
                        or caster_progression_after_kill["Experience"] != 120
                        or caster_progression_after_kill["Health"] != 100
                    ):
                        log(
                            "  Client_QueryProgression after monster kill returned unexpected payload: "
                            f"{caster_progression_after_kill}"
                        )
                        return False
                    log(f"  Client_QueryProgression after monster kill OK: {caster_progression_after_kill}")

                if 43 in enabled_tests:
                    log("Test 43: dead monster cannot be targeted again...")
                    if spawned_monster_unit is None:
                        log("  dead monster target test skipped: monster was not spawned")
                        return False
                    cast_skill_at_unit_payload = (
                        struct.pack("<Q", player_id)
                        + pack_combat_unit_ref(
                            spawned_monster_unit["UnitKind"],
                            spawned_monster_unit["CombatEntityId"],
                            spawned_monster_unit["PlayerId"],
                        )
                        + struct.pack("<I", 1001)
                    )
                    dead_monster_response = parse_cast_skill_at_unit_response(
                        call_client_function(sock, "Client_CastSkillAtUnit", cast_skill_at_unit_payload)
                    )
                    if dead_monster_response["bSuccess"] or dead_monster_response["Error"] != "scene_combat_target_not_found":
                        log(f"  dead monster retarget returned unexpected payload: {dead_monster_response}")
                        return False
                    log(f"  dead monster retarget OK: {dead_monster_response}")

                if 18 in enabled_tests:
                    log("Test 18: second player receives first player move update...")
                    move_again_response = parse_pawn_state_response(
                        call_client_function(sock, "Client_Move", struct.pack("<Qfff", player_id, 256.0, 96.0, 11.0))
                    )
                    if not move_again_response["bSuccess"]:
                        log(f"  second move failed: error={move_again_response['Error']}")
                        return False

                    update_downlink = parse_scene_state_message(
                        recv_client_downlink(sock2, "Client_ScenePlayerUpdate", 5.0)
                    )
                    if (
                        update_downlink["PlayerId"] != player_id
                        or update_downlink["SceneId"] != next_scene_id
                        or abs(update_downlink["X"] - 256.0) > 0.001
                        or abs(update_downlink["Y"] - 96.0) > 0.001
                        or abs(update_downlink["Z"] - 11.0) > 0.001
                    ):
                        log(f"  Client_ScenePlayerUpdate returned unexpected payload: {update_downlink}")
                        return False
                    log(f"  Client_ScenePlayerUpdate OK: {update_downlink}")

                if 19 in enabled_tests:
                    log("Test 19: Client_Logout...")
                    logout_response = parse_logout_response(
                        call_client_function(sock, "Client_Logout", struct.pack("<Q", player_id))
                    )
                    if not logout_response["bSuccess"]:
                        log(f"  Client_Logout failed: error={logout_response['Error']}")
                        return False
                    if logout_response["PlayerId"] != player_id:
                        log(f"  Client_Logout returned unexpected playerId={logout_response['PlayerId']}")
                        return False
                    log(f"  Client_Logout OK: playerId={logout_response['PlayerId']}")

                if 20 in enabled_tests:
                    log("Test 20: second player receives leave downlink...")
                    leave_downlink = parse_scene_leave_message(
                        recv_client_downlink(sock2, "Client_ScenePlayerLeave", 5.0)
                    )
                    if leave_downlink["PlayerId"] != player_id or leave_downlink["SceneId"] != next_scene_id:
                        log(f"  Client_ScenePlayerLeave returned unexpected payload: {leave_downlink}")
                        return False
                    log(f"  Client_ScenePlayerLeave OK: {leave_downlink}")

                if 21 in enabled_tests:
                    log("Test 21: Client_FindPlayer after logout...")
                    find_after_logout = parse_find_player_response(
                        call_client_function(sock, "Client_FindPlayer", struct.pack("<Q", player_id))
                    )
                    if find_after_logout["bFound"]:
                        log(
                            "  Client_FindPlayer after logout should not find player, "
                            f"but got sceneId={find_after_logout['SceneId']}"
                        )
                        return False
                    log("  Client_FindPlayer after logout OK: player removed from World state")

                if 22 in enabled_tests:
                    log("Test 22: Client_Login again after logout...")
                    relogin_response = parse_login_response(
                        call_client_function(sock, "Client_Login", struct.pack("<Q", player_id))
                    )
                    if not relogin_response["bSuccess"]:
                        log(f"  second Client_Login failed: error={relogin_response['Error']}")
                        return False
                    if relogin_response["PlayerId"] != player_id or relogin_response["SessionKey"] == 0:
                        log(
                            "  second Client_Login returned unexpected payload: "
                            f"playerId={relogin_response['PlayerId']} sessionKey={relogin_response['SessionKey']}"
                        )
                        return False
                    if initial_session_key != 0 and relogin_response["SessionKey"] == initial_session_key:
                        log(
                            "  second Client_Login should allocate a fresh session key, "
                            f"but reused {relogin_response['SessionKey']}"
                        )
                        return False
                    log(
                        "  second Client_Login OK: "
                        f"playerId={relogin_response['PlayerId']} sessionKey={relogin_response['SessionKey']}"
                    )

                if 23 in enabled_tests:
                    log("Test 23: Client_FindPlayer after relogin...")
                    find_after_relogin = parse_find_player_response(
                        call_client_function(sock, "Client_FindPlayer", struct.pack("<Q", player_id))
                    )
                    if not find_after_relogin["bFound"] or find_after_relogin["SceneId"] != next_scene_id:
                        log(
                            "  Client_FindPlayer after relogin returned unexpected payload: "
                            f"found={find_after_relogin['bFound']} sceneId={find_after_relogin['SceneId']} "
                            f"error={find_after_relogin['Error']}"
                        )
                        return False
                    log(
                        "  Client_FindPlayer after relogin OK: "
                        f"playerId={find_after_relogin['PlayerId']} sceneId={find_after_relogin['SceneId']}"
                    )

                if 24 in enabled_tests:
                    log("Test 24: Client_QueryProfile after relogin...")
                    query_profile_after_relogin = parse_query_profile_response(
                        call_client_function(sock, "Client_QueryProfile", struct.pack("<Q", player_id))
                    )
                    if (
                        not query_profile_after_relogin["bSuccess"]
                        or query_profile_after_relogin["PlayerId"] != player_id
                        or query_profile_after_relogin["CurrentSceneId"] != next_scene_id
                        or query_profile_after_relogin["Gold"] != 50
                        or query_profile_after_relogin["EquippedItem"] != "iron_sword"
                        or query_profile_after_relogin["Level"] != 3
                        or query_profile_after_relogin["Experience"] != 250
                        or query_profile_after_relogin["Health"] != 75
                    ):
                        log(
                            "  Client_QueryProfile after relogin returned unexpected payload: "
                            f"{query_profile_after_relogin}"
                        )
                        return False
                    log(f"  Client_QueryProfile after relogin OK: {query_profile_after_relogin}")

                if 25 in enabled_tests:
                    log("Test 25: Client_QueryInventory after relogin...")
                    query_inventory_after_relogin = parse_query_inventory_response(
                        call_client_function(sock, "Client_QueryInventory", struct.pack("<Q", player_id))
                    )
                    if (
                        not query_inventory_after_relogin["bSuccess"]
                        or query_inventory_after_relogin["PlayerId"] != player_id
                        or query_inventory_after_relogin["Gold"] != 50
                        or query_inventory_after_relogin["EquippedItem"] != "iron_sword"
                    ):
                        log(
                            "  Client_QueryInventory after relogin returned unexpected payload: "
                            f"{query_inventory_after_relogin}"
                        )
                        return False
                    log(f"  Client_QueryInventory after relogin OK: {query_inventory_after_relogin}")

                if 26 in enabled_tests:
                    log("Test 26: Client_QueryProgression after relogin...")
                    query_progression_after_relogin = parse_query_progression_response(
                        call_client_function(sock, "Client_QueryProgression", struct.pack("<Q", player_id))
                    )
                    if (
                        not query_progression_after_relogin["bSuccess"]
                        or query_progression_after_relogin["PlayerId"] != player_id
                        or query_progression_after_relogin["Level"] != 3
                        or query_progression_after_relogin["Experience"] != 250
                        or query_progression_after_relogin["Health"] != 75
                    ):
                        log(
                            "  Client_QueryProgression after relogin returned unexpected payload: "
                            f"{query_progression_after_relogin}"
                        )
                        return False
                    log(f"  Client_QueryProgression after relogin OK: {query_progression_after_relogin}")

                if 33 in enabled_tests:
                    log("Test 33: Client_QueryCombatProfile after relogin...")
                    query_combat_profile_after_relogin = parse_query_combat_profile_response(
                        call_client_function(sock, "Client_QueryCombatProfile", struct.pack("<Q", player_id))
                    )
                    if (
                        not query_combat_profile_after_relogin["bSuccess"]
                        or query_combat_profile_after_relogin["PlayerId"] != player_id
                        or query_combat_profile_after_relogin["BaseAttack"] != 10
                        or query_combat_profile_after_relogin["BaseDefense"] != 5
                        or query_combat_profile_after_relogin["MaxHealth"] != 100
                        or query_combat_profile_after_relogin["PrimarySkillId"] != 2002
                        or query_combat_profile_after_relogin["LastResolvedSceneId"] != 0
                        or query_combat_profile_after_relogin["LastResolvedHealth"] != 100
                    ):
                        log(
                            "  Client_QueryCombatProfile after relogin returned unexpected payload: "
                            f"{query_combat_profile_after_relogin}"
                        )
                        return False
                    log(f"  Client_QueryCombatProfile after relogin OK: {query_combat_profile_after_relogin}")

                if 27 in enabled_tests:
                    log("Test 27: forwarded Client_FindPlayer invalid payload...")
                    invalid_payload_response = try_find_player(sock, struct.pack("<I", player_id))
                    if invalid_payload_response is None:
                        log("  invalid payload test failed: no response")
                        return False
                    if invalid_payload_response["Error"] != "client_call_param_binding_failed":
                        log(
                            "  invalid payload returned unexpected error: "
                            f"{invalid_payload_response['Error']}"
                        )
                        return False
                    log("  invalid payload OK: binder error surfaced through Gateway")

                if 28 in enabled_tests:
                    log("Test 28: forwarded Client_FindPlayer validation error...")
                    zero_player_response = try_find_player(sock, struct.pack("<Q", 0))
                    if zero_player_response is None:
                        log("  zero player id test failed: no response")
                        return False
                    if zero_player_response["Error"] != "player_id_required":
                        log(
                            "  zero player id returned unexpected error: "
                            f"{zero_player_response['Error']}"
                        )
                        return False
                    log("  validation error OK: world business error reached client")

                if 29 in enabled_tests:
                    log("Test 29: World unavailable after forward route...")
                    world_proc = proc_by_name.get("WorldServer")
                    if world_proc is None:
                        log("  WorldServer process handle missing")
                        return False
                    try:
                        world_proc.terminate()
                        world_proc.wait(timeout=5)
                    except Exception:
                        try:
                            world_proc.kill()
                        except Exception:
                            pass
                    backend_unavailable_response = wait_until(
                        time.time() + 5.0,
                        lambda: try_find_player(sock, struct.pack("<Q", player_id), timeout=1.0),
                    )
                    if backend_unavailable_response is None:
                        log("  backend unavailable test failed: no response")
                        return False
                    if backend_unavailable_response["Error"] not in {
                        "client_route_backend_unavailable",
                        "server_call_disconnected",
                        "server_call_send_failed",
                    }:
                        log(
                            "  backend unavailable returned unexpected error: "
                            f"{backend_unavailable_response['Error']}"
                        )
                        return False
                    log("  backend unavailable OK: Gateway returned reflected route error")

                log("Validation PASSED")
                return True
            finally:
                try:
                    sock.close()
                except OSError:
                    pass
                try:
                    sock2.close()
                except OSError:
                    pass
        finally:
            cleanup()
