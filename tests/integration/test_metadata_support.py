import time

from arcticc.version_store import NativeVersionStore, VersionedItem

import numpy as np
from pandas import DataFrame, Timestamp
from pandas.util.testing import assert_frame_equal
import pytest
from arcticc.exceptions import ArcticNativeNotYetImplemented
from arcticcxx.storage import NoDataFoundException


# In the following lines, the naming convention is
# test_rt_df stands for roundtrip dataframe (implicitly pandas given file name)


@pytest.mark.parametrize("lib_type", ["lmdb_version_store", "s3_version_store"])
def test_rt_df_with_small_meta(lib_type, request):
    lib = request.getfixturevalue(lib_type)
    #  type: (NativeVersionStore)->None
    df = DataFrame(data=["A", "B", "C"])
    meta = {"abc": "def", "xxx": "yyy"}
    lib.write("pandas", df, metadata=meta)
    vit = lib.read("pandas")
    assert_frame_equal(df, vit.data)
    assert meta == vit.metadata


@pytest.mark.parametrize("lib_type", ["lmdb_version_store", "s3_version_store"])
def test_rt_df_with_humonguous_meta(lib_type, request):
    with pytest.raises(ArcticNativeNotYetImplemented):
        from arcticc.version_store._normalization import _MAX_USER_DEFINED_META as MAX

        lib = request.getfixturevalue(lib_type)
        df = DataFrame(data=["A", "B", "C"])
        meta = {"a": "x" * (MAX)}
        lib.write("pandas", df, metadata=meta)

        vit = lib.read("pandas")
        assert_frame_equal(df, vit)
        assert meta == vit.metadata


@pytest.mark.parametrize("lib_type", ["mongo_version_store", "lmdb_version_store", "s3_version_store"])
def test_read_metadata(lib_type, request):
    lib = request.getfixturevalue(lib_type)
    original_data = [1, 2, 3]
    snap_name = "metadata_snap_1"
    symbol = "test_symbol"
    metadata = {"something": 1}
    lib.write(symbol, original_data, metadata={"something": 1})
    lib.snapshot(snap_name, metadata={"snap_meta": 1})
    assert lib.read_metadata("test_symbol").metadata == metadata


@pytest.mark.parametrize("lib_type", ["lmdb_version_store", "s3_version_store"])
def test_read_metadata_by_version(lib_type, request):
    lib = request.getfixturevalue(lib_type)
    data_v1 = [1, 2, 3]
    data_v2 = [10, 20, 30]
    symbol = "test_symbol"
    metadata_v0 = {"something": 1}
    metadata_v1 = {"something more": 2}
    lib.write(symbol, data_v1, metadata=metadata_v0)
    lib.write(symbol, data_v2, metadata=metadata_v1)
    assert lib.read_metadata(symbol).metadata == metadata_v1
    assert lib.read_metadata(symbol, 0).metadata == metadata_v0
    assert lib.read_metadata(symbol, 1).metadata == metadata_v1


def test_read_metadata_by_snapshot(lmdb_version_store):
    original_data = [1, 2, 3]
    snap_name = "metadata_snap_1"
    symbol = "test_symbol"
    metadata = {"something": 1}
    lmdb_version_store.write(symbol, original_data, metadata={"something": 1})
    lmdb_version_store.snapshot(snap_name, metadata={"snap_meta": 1})
    assert lmdb_version_store.read_metadata(symbol).metadata == metadata
    assert lmdb_version_store.read_metadata(symbol, snap_name).metadata == metadata


def test_read_metadata_by_timestamp(lmdb_version_store):
    symbol = "test_symbol"

    metadata_v0 = {"something": 1}
    lmdb_version_store.write(symbol, 1, metadata=metadata_v0)  # v0
    time_after_first_write = Timestamp.utcnow()
    time.sleep(0.1)

    with pytest.raises(NoDataFoundException):
        lmdb_version_store.read(symbol, as_of=Timestamp(0))

    assert lmdb_version_store.read_metadata(symbol, as_of=time_after_first_write).metadata == metadata_v0

    metadata_v1 = {"something more": 2}
    lmdb_version_store.write(symbol, 2, metadata=metadata_v1)  # v1
    time.sleep(0.11)

    metadata_v2 = {"something else": 3}
    lmdb_version_store.write(symbol, 3, metadata=metadata_v2)  # v2
    time.sleep(0.1)

    metadata_v3 = {"nothing": 4}
    lmdb_version_store.write(symbol, 4, metadata=metadata_v3)  # v3

    versions = lmdb_version_store.list_versions()
    assert len(versions) == 4
    sorted_versions_for_a = sorted([v for v in versions if v["symbol"] == symbol], key=lambda x: x["version"])

    assert lmdb_version_store.read_metadata(symbol, as_of=time_after_first_write).metadata == metadata_v0

    ts_for_v1 = sorted_versions_for_a[1]["date"]
    assert lmdb_version_store.read_metadata(symbol, as_of=ts_for_v1).metadata == metadata_v1

    ts_for_v2 = sorted_versions_for_a[2]["date"]
    assert lmdb_version_store.read_metadata(symbol, as_of=ts_for_v2).metadata == metadata_v2

    with pytest.raises(NoDataFoundException):
        lmdb_version_store.read(symbol, as_of=Timestamp(0))

    brexit_almost_over = Timestamp(np.iinfo(np.int64).max)  # Timestamp("2262-04-11 23:47:16.854775807")
    assert lmdb_version_store.read_metadata(symbol, as_of=brexit_almost_over).metadata == metadata_v3


def test_write_metadata_first_write(lmdb_version_store, sym):
    metadata_v0 = {"something": 1}
    # lmdb_version_store.write(symbol, 1, metadata=metadata_v0)  # v0
    lmdb_version_store.write_metadata(sym, metadata_v0)
    vitem = lmdb_version_store.read(sym)
    assert vitem.data is None
    assert vitem.metadata == metadata_v0
    assert len(lmdb_version_store.list_versions(sym)) == 1


def test_write_metadata_preexisting_symbol(lmdb_version_store, sym):
    lib = lmdb_version_store
    metadata_v0 = {"something": 1}
    metadata_v1 = {"something": 2}
    # lmdb_version_store.write(symbol, 1, metadata=metadata_v0)  # v0
    lib.write(sym, 1, metadata=metadata_v0)
    vi = lib.write_metadata(sym, metadata_v1)
    assert vi.version == 1
    assert isinstance(vi, VersionedItem)
    assert lib.read(sym).metadata == metadata_v1
    assert lib.read(sym).data == 1


def test_write_metadata_preexisting_symbol_no_pruning(lmdb_version_store, sym):
    lib = lmdb_version_store
    metadata_v0 = {"something": 1}
    metadata_v1 = {"something": 2}
    # lmdb_version_store.write(symbol, 1, metadata=metadata_v0)  # v0
    lib.write(sym, 1, metadata=metadata_v0)
    lib.write_metadata(sym, metadata_v1, prune_previous_version=False)
    assert lib.read(sym).metadata == metadata_v1
    assert lib.read(sym).data == 1
    assert lib.read(sym, as_of=0).metadata == metadata_v0
    assert lib.read(sym, as_of=0).data == 1
