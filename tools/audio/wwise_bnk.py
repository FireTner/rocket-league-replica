"""Read a Wwise soundbank far enough to map an event id to its media blobs.

Measured on the local banks (BKHD version 150): an event is a count of action
ids, an action's target id sits at byte 6, a sound's source id sits at byte 9,
and a container's children are the longest run of object ids in its body.
"""

import struct


class BankError(RuntimeError):
    pass


def fnv1(text):
    """Wwise short id: FNV-1 over the lowercased ASCII name."""
    digest = 2166136261
    for byte in text.lower().encode("ascii"):
        digest = ((digest * 16777619) & 0xFFFFFFFF) ^ byte
    return digest


def _chunks(data):
    pos = 0
    found = {}
    while pos + 8 <= len(data):
        kind = data[pos:pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        if size == 0 or pos + 8 + size > len(data):
            break
        if not all(32 < b < 127 for b in kind):
            break
        found[kind] = data[pos + 8:pos + 8 + size]
        pos += 8 + size
    return found


class Bank(object):
    def __init__(self, data):
        chunks = _chunks(data)
        if b"HIRC" not in chunks or b"DIDX" not in chunks or b"DATA" not in chunks:
            raise BankError("bank is missing HIRC, DIDX, or DATA")
        self._data = chunks[b"DATA"]
        self._media = {}
        didx = chunks[b"DIDX"]
        if len(didx) % 12:
            raise BankError("DIDX length is not a multiple of 12")
        for i in range(0, len(didx), 12):
            mid, offset, size = struct.unpack_from("<III", didx, i)
            if offset + size > len(self._data):
                raise BankError("media %08x overruns DATA" % mid)
            self._media[mid] = (offset, size)
        self._obj = {}
        hirc = chunks[b"HIRC"]
        if len(hirc) < 4:
            raise BankError("empty HIRC")
        count = struct.unpack_from("<I", hirc, 0)[0]
        pos = 4
        for _ in range(count):
            if pos + 5 > len(hirc):
                raise BankError("HIRC object header overruns the chunk")
            kind = hirc[pos]
            pos += 1
            size = struct.unpack_from("<I", hirc, pos)[0]
            pos += 4
            if size < 4 or pos + size > len(hirc):
                raise BankError("HIRC object overruns the chunk")
            body = hirc[pos:pos + size]
            pos += size
            oid = struct.unpack_from("<I", body, 0)[0]
            self._obj[oid] = (kind, body)

    def media(self, mid):
        offset, size = self._media[mid]
        return self._data[offset:offset + size]

    def event_id(self, name):
        eid = fnv1(name)
        kind, _body = self._obj.get(eid, (None, None))
        if kind != 4:
            return None
        return eid

    def leaves(self, eid, limit=4):
        """Source-media ids reached from event `eid`, depth-first, capped."""
        out = []
        self._walk(eid, set(), out, limit)
        return out

    def _walk(self, oid, seen, out, limit):
        if oid in seen or len(out) >= limit:
            return
        seen.add(oid)
        kind, body = self._obj.get(oid, (None, None))
        if kind is None or body is None:
            return
        if kind == 4:
            count = body[4] if len(body) > 4 else 0
            if 5 + count * 4 > len(body) or count > 16:
                return
            for i in range(count):
                action = struct.unpack_from("<I", body, 5 + i * 4)[0]
                self._walk(action, seen, out, limit)
            return
        if kind == 3:
            if len(body) < 10:
                return
            self._walk(struct.unpack_from("<I", body, 6)[0], seen, out, limit)
            return
        if kind == 2:
            if len(body) >= 13:
                mid = struct.unpack_from("<I", body, 9)[0]
                if mid in self._media:
                    out.append(mid)
            return
        for child in self._children(oid, body):
            self._walk(child, seen, out, limit)
            if len(out) >= limit:
                return

    def _children(self, oid, body):
        """Longest run of `count` object ids. Containers store their kids that way."""
        best = []
        blob = body[4:]
        known = self._obj
        last = len(blob) - 4
        for off in range(0, last):
            count = struct.unpack_from("<I", blob, off)[0]
            if count < 1 or count > 32 or off + 4 + count * 4 > len(blob):
                continue
            seq = []
            ok = True
            for k in range(count):
                cid = struct.unpack_from("<I", blob, off + 4 + k * 4)[0]
                if cid == oid or cid not in known:
                    ok = False
                    break
                seq.append(cid)
            if ok and len(seq) > len(best):
                best = seq
        return best
