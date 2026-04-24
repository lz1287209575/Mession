from __future__ import annotations

from typing import Any, Dict, List

from validation.schema_loader import FieldSchema, SchemaRegistry, StructSchema, ValidationSchemaError

from .reflect import ReflectReader


class ReflectionDecoder:
    def __init__(self, registry: SchemaRegistry):
        self.registry = registry

    def decode_struct(self, type_name: str, payload: bytes) -> Dict[str, Any]:
        reader = ReflectReader(payload)
        value = self.decode_struct_from_reader(type_name, reader)
        reader.ensure_consumed()
        return value

    def decode_struct_from_reader(self, type_name: str, reader: ReflectReader) -> Dict[str, Any]:
        schema = self.registry.get_struct(type_name)
        return self._decode_struct(schema, reader)

    def _decode_struct(self, schema: StructSchema, reader: ReflectReader) -> Dict[str, Any]:
        result: Dict[str, Any] = {}
        for field in schema.fields:
            value = self._decode_field(field, reader)
            if field.kind != "padding":
                result[field.name] = value
        return result

    def _decode_field(self, field: FieldSchema, reader: ReflectReader) -> Any:
        if field.kind == "bool":
            return reader.read_bool()
        if field.kind == "i8":
            return reader.read_i8()
        if field.kind == "u8":
            return reader.read_u8()
        if field.kind == "i16":
            return reader.read_i16()
        if field.kind == "u16":
            return reader.read_u16()
        if field.kind == "i32":
            return reader.read_i32()
        if field.kind == "u32":
            return reader.read_u32()
        if field.kind == "i64":
            return reader.read_i64()
        if field.kind == "u64":
            return reader.read_u64()
        if field.kind == "f32":
            return reader._read("<f")
        if field.kind == "f64":
            return reader._read("<d")
        if field.kind == "string":
            return reader.read_string()
        if field.kind == "bytes":
            if field.size is None or field.size < 0:
                raise ValidationSchemaError(f"field '{field.name}' requires non-negative 'size'")
            return reader.read_bytes(field.size)
        if field.kind == "padding":
            if field.size is None or field.size < 0:
                raise ValidationSchemaError(f"padding field '{field.name}' requires non-negative 'size'")
            reader.read_bytes(field.size)
            return None
        if field.kind == "struct":
            if not field.type_name:
                raise ValidationSchemaError(f"struct field '{field.name}' requires 'type_name'")
            return self.decode_struct_from_reader(field.type_name, reader)
        if field.kind == "vector":
            return self._decode_vector(field, reader)
        raise ValidationSchemaError(f"unsupported field kind '{field.kind}' on '{field.name}'")

    def _decode_vector(self, field: FieldSchema, reader: ReflectReader) -> List[Any]:
        if not field.item_kind:
            raise ValidationSchemaError(f"vector field '{field.name}' requires 'item_kind'")
        count = reader.read_u32()
        items: List[Any] = []
        for _ in range(count):
            if field.item_kind == "struct":
                if not field.item_type_name:
                    raise ValidationSchemaError(
                        f"vector field '{field.name}' with struct items requires 'item_type_name'"
                    )
                items.append(self.decode_struct_from_reader(field.item_type_name, reader))
                continue
            items.append(self._decode_field(FieldSchema(name=field.name, kind=field.item_kind), reader))
        return items
