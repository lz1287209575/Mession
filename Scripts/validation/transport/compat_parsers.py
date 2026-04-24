"""
Temporary compatibility layer for legacy response parsers.

The long-term direction is schema-driven decoding via:

- `validation.schema_loader`
- `validation.transport.reflection_decoder`

This module hosts compatibility helpers that already consume a schema-backed
decoder so `Scripts/validate.py` can migrate incrementally without growing more
hand-written `parse_*_response(...)` functions.
"""

from __future__ import annotations

from functools import lru_cache
from pathlib import Path
from typing import Any, Dict

from validation.schema_loader import load_schema_with_fallback

from .reflection_decoder import ReflectionDecoder


@lru_cache(maxsize=1)
def _compat_decoder() -> ReflectionDecoder:
    project_root = Path(__file__).resolve().parents[3]
    registry = load_schema_with_fallback(project_root)
    return ReflectionDecoder(registry)


def decode_struct(type_name: str, payload: bytes) -> Dict[str, Any]:
    return _compat_decoder().decode_struct(type_name, payload)


def parse_query_inventory_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientQueryInventoryResponse", payload)


def parse_query_progression_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientQueryProgressionResponse", payload)


def parse_login_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientLoginResponse", payload)


def parse_find_player_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientFindPlayerResponse", payload)


def parse_logout_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientLogoutResponse", payload)


def parse_switch_scene_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientSwitchSceneResponse", payload)


def parse_change_gold_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientChangeGoldResponse", payload)


def parse_query_profile_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientQueryProfileResponse", payload)


def parse_query_combat_profile_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientQueryCombatProfileResponse", payload)


def parse_set_primary_skill_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientSetPrimarySkillResponse", payload)


def parse_debug_spawn_monster_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientDebugSpawnMonsterResponse", payload)


def parse_cast_skill_at_unit_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientCastSkillAtUnitResponse", payload)


def parse_create_party_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientCreatePartyResponse", payload)


def parse_invite_party_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientInvitePartyResponse", payload)


def parse_accept_party_invite_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientAcceptPartyInviteResponse", payload)


def parse_kick_party_member_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientKickPartyMemberResponse", payload)


def parse_party_created_notify(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientPartyCreatedNotify", payload)


def parse_party_invite_received_notify(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientPartyInviteReceivedNotify", payload)


def parse_party_member_joined_notify(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientPartyMemberJoinedNotify", payload)


def parse_party_member_removed_notify(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientPartyMemberRemovedNotify", payload)


def parse_pawn_state_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientQueryPawnResponse", payload)


def parse_scene_state_message(payload: bytes) -> Dict[str, Any]:
    return decode_struct("SPlayerSceneStateMessage", payload)


def parse_scene_leave_message(payload: bytes) -> Dict[str, Any]:
    return decode_struct("SPlayerSceneLeaveMessage", payload)


def parse_equip_item_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientEquipItemResponse", payload)


def parse_grant_experience_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientGrantExperienceResponse", payload)


def parse_modify_health_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientModifyHealthResponse", payload)


def parse_cast_skill_response(payload: bytes) -> Dict[str, Any]:
    return decode_struct("FClientCastSkillResponse", payload)


__all__ = [
    "decode_struct",
    "parse_accept_party_invite_response",
    "parse_cast_skill_at_unit_response",
    "parse_cast_skill_response",
    "parse_change_gold_response",
    "parse_create_party_response",
    "parse_debug_spawn_monster_response",
    "parse_equip_item_response",
    "parse_find_player_response",
    "parse_grant_experience_response",
    "parse_invite_party_response",
    "parse_kick_party_member_response",
    "parse_login_response",
    "parse_logout_response",
    "parse_modify_health_response",
    "parse_party_created_notify",
    "parse_party_invite_received_notify",
    "parse_party_member_joined_notify",
    "parse_party_member_removed_notify",
    "parse_pawn_state_response",
    "parse_query_combat_profile_response",
    "parse_query_inventory_response",
    "parse_query_profile_response",
    "parse_query_progression_response",
    "parse_scene_leave_message",
    "parse_scene_state_message",
    "parse_set_primary_skill_response",
    "parse_switch_scene_response",
]
