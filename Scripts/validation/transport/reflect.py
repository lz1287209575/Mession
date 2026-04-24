from __future__ import annotations

import struct
from typing import List


class ReflectReader:
    def __init__(self, data: bytes):
        self.data = data
        self.offset = 0

    def _read(self, fmt: str):
        size = struct.calcsize(fmt)
        if self.offset + size > len(self.data):
            raise ValueError(f"read overflow: need={size} offset={self.offset} size={len(self.data)}")
        value = struct.unpack_from(fmt, self.data, self.offset)[0]
        self.offset += size
        return value

    def read_bool(self) -> bool:
        return self._read("<B") != 0

    def read_u8(self) -> int:
        return self._read("<B")

    def read_i8(self) -> int:
        return self._read("<b")

    def read_u16(self) -> int:
        return self._read("<H")

    def read_i16(self) -> int:
        return self._read("<h")

    def read_u32(self) -> int:
        return self._read("<I")

    def read_i32(self) -> int:
        return self._read("<i")

    def read_u64(self) -> int:
        return self._read("<Q")

    def read_i64(self) -> int:
        return self._read("<q")

    def read_bytes(self, size: int) -> bytes:
        if self.offset + size > len(self.data):
            raise ValueError(f"bytes overflow: need={size} offset={self.offset} size={len(self.data)}")
        raw = self.data[self.offset:self.offset + size]
        self.offset += size
        return raw

    def read_string(self) -> str:
        size = self.read_u32()
        if self.offset + size > len(self.data):
            raise ValueError(f"string overflow: need={size} offset={self.offset} size={len(self.data)}")
        raw = self.data[self.offset:self.offset + size]
        self.offset += size
        return raw.decode("utf-8", errors="replace")

    def read_u64_vector(self) -> List[int]:
        size = self.read_u32()
        result: List[int] = []
        for _ in range(size):
            result.append(self.read_u64())
        return result

    def ensure_consumed(self) -> None:
        if self.offset != len(self.data):
            raise ValueError(f"trailing bytes: offset={self.offset}, size={len(self.data)}")
