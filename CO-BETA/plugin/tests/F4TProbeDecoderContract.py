import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import tempfile

ROOT = Path(__file__).resolve().parents[3]
DECODER_PATH = ROOT / "scripts/decode-probe-ring.py"

spec = importlib.util.spec_from_file_location("decode_probe_ring", DECODER_PATH)
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)

record = decoder.Record(
    version=decoder.RECORD_VERSION,
    probe_id=3,
    kind=2,
    thread_id=5,
    tick_micros=7,
    caller_rva=11,
    args=(2, 3, 5, 7, 11, 13),
    retval=17,
    arg_count=3,
)

encoded = decoder.encode_record(record)
assert len(encoded) == decoder.RECORD_SIZE

data = decoder.make_file([record], overwritten=5)
records, blocks, overwritten = decoder.decode(data)
assert len(records) == 1
assert blocks == 1
assert overwritten == 5

decoded = records[0]
assert decoded.probe_id == record.probe_id
assert decoded.kind == record.kind
assert decoded.thread_id == record.thread_id
assert decoded.tick_micros == record.tick_micros
assert decoded.caller_rva == record.caller_rva
assert decoded.args == record.args
assert decoded.retval == record.retval
assert decoded.arg_count == record.arg_count

two = decoder.make_file([record, record], overwritten=0)
records, blocks, _ = decoder.decode(two)
assert len(records) == 2
assert blocks == 1

for mutate in (
    lambda value: b"XXXX" + value[4:],
    lambda value: value[:4] + struct.pack("<I", decoder.FILE_VERSION + 1) + value[8:],
    lambda value: value[:-1],
    lambda value: value[:8] + b"XXXX" + value[12:],
):
    try:
        decoder.decode(mutate(data))
    except decoder.DumpError:
        pass
    else:
        raise AssertionError("a malformed recording was accepted")

named = decoder.Record(
    version=decoder.RECORD_VERSION,
    probe_id=7,
    kind=2,
    thread_id=5,
    tick_micros=7,
    caller_rva=11,
    args=(0x1000, 0x2, 0xFFFFFFFFFFFFFFF0, 0x2000, 0x99),
    retval=0,
    arg_count=5,
)
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder) / "probe.bin"
    path.write_bytes(decoder.make_file([named]))
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        assert decoder.main([str(path)]) == 0
text = output.getvalue()
assert "name=res349-taskunpack-case4a" in text, text
assert "taskRef8=0x0000000000002000" in text, text
assert "records=1 blocks=1 overwritten=0" in text, text

# TODO-verify(#407)
dispatch = decoder.format_record(decoder.Record(
    version=decoder.RECORD_VERSION,
    probe_id=3,
    kind=2,
    thread_id=5,
    tick_micros=7,
    caller_rva=11,
    args=(0x125F3C3, 0x9ABC, 0x1234, 0x5678),
    retval=0,
    arg_count=4,
))
assert "name=res408-command-execute-dispatch" in dispatch, dispatch
assert "bridgeRdi=0x0000000000009ABC" in dispatch, dispatch
assert "dispatchRax=0x0000000000001234" in dispatch, dispatch
assert "dispatchTargetRva=0x0000000000005678" in dispatch, dispatch
assert "vtable=" not in dispatch, dispatch

# TODO-verify(#407)
bridge = decoder.format_record(decoder.Record(
    version=decoder.RECORD_VERSION,
    probe_id=13,
    kind=2,
    thread_id=5,
    tick_micros=7,
    caller_rva=11,
    args=(0x125F396, 0),
    retval=0,
    arg_count=2,
))
assert "name=res553-vmbridge-read" in bridge, bridge
assert "bridgeRdi=0x0000000000000000" in bridge, bridge

print("probe decoder contract PASS")
