from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Optional


DEFAULT_SCHEMA_PATH = Path("Build/Generated/ValidationProtocolSchema.json")
BUNDLED_COMPAT_SCHEMA_PATH = Path("Scripts/validation/schemas/compat_protocol_schema.json")


class ValidationSchemaError(ValueError):
    pass


@dataclass(frozen=True)
class FieldSchema:
    name: str
    kind: str
    type_name: Optional[str] = None
    item_kind: Optional[str] = None
    item_type_name: Optional[str] = None
    size: Optional[int] = None

    @classmethod
    def from_dict(cls, payload: Dict[str, Any]) -> "FieldSchema":
        name = str(payload.get("name", ""))
        kind = str(payload.get("kind", ""))
        if not name:
            raise ValidationSchemaError("field schema requires non-empty 'name'")
        if not kind:
            raise ValidationSchemaError(f"field '{name}' requires non-empty 'kind'")
        size_value = payload.get("size")
        size = None if size_value is None else int(size_value)
        return cls(
            name=name,
            kind=kind,
            type_name=_optional_string(payload.get("type_name")),
            item_kind=_optional_string(payload.get("item_kind")),
            item_type_name=_optional_string(payload.get("item_type_name")),
            size=size,
        )


@dataclass(frozen=True)
class StructSchema:
    name: str
    fields: tuple[FieldSchema, ...]

    @classmethod
    def from_dict(cls, name: str, payload: Dict[str, Any]) -> "StructSchema":
        raw_fields = payload.get("fields")
        if not isinstance(raw_fields, list) or not raw_fields:
            raise ValidationSchemaError(f"struct '{name}' requires a non-empty 'fields' list")
        fields = tuple(FieldSchema.from_dict(item) for item in raw_fields)
        return cls(name=name, fields=fields)


@dataclass(frozen=True)
class SchemaRegistry:
    schema_version: int
    producer: Optional[str]
    generated_at: Optional[str]
    structs: Dict[str, StructSchema]

    def get_struct(self, type_name: str) -> StructSchema:
        schema = self.structs.get(type_name)
        if schema is None:
            raise ValidationSchemaError(f"unknown struct schema '{type_name}'")
        return schema

    @classmethod
    def from_dict(cls, payload: Dict[str, Any]) -> "SchemaRegistry":
        raw_structs = payload.get("structs")
        if not isinstance(raw_structs, dict) or not raw_structs:
            raise ValidationSchemaError("schema requires a non-empty 'structs' object")

        structs: Dict[str, StructSchema] = {}
        for type_name, raw_struct in raw_structs.items():
            if not isinstance(raw_struct, dict):
                raise ValidationSchemaError(f"struct '{type_name}' definition must be an object")
            structs[str(type_name)] = StructSchema.from_dict(str(type_name), raw_struct)

        return cls(
            schema_version=int(payload.get("schema_version", 1)),
            producer=_optional_string(payload.get("producer")),
            generated_at=_optional_string(payload.get("generated_at")),
            structs=structs,
        )


def load_schema_file(schema_path: Path) -> SchemaRegistry:
    try:
        payload = json.loads(schema_path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ValidationSchemaError(f"schema file not found: {schema_path}") from exc
    except json.JSONDecodeError as exc:
        raise ValidationSchemaError(f"invalid schema json: {schema_path}: {exc}") from exc
    return SchemaRegistry.from_dict(payload)


def load_default_schema(project_root: Path) -> SchemaRegistry:
    return load_schema_file((project_root / DEFAULT_SCHEMA_PATH).resolve())


def load_bundled_compat_schema(project_root: Path) -> SchemaRegistry:
    return load_schema_file((project_root / BUNDLED_COMPAT_SCHEMA_PATH).resolve())


def load_schema_with_fallback(project_root: Path) -> SchemaRegistry:
    generated_path = (project_root / DEFAULT_SCHEMA_PATH).resolve()
    if generated_path.exists():
        return load_schema_file(generated_path)
    return load_bundled_compat_schema(project_root)


def _optional_string(value: Any) -> Optional[str]:
    if value is None:
        return None
    text = str(value)
    return text if text else None
