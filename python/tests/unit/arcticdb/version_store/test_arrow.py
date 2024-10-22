from arcticdb_ext.version_store import OutputFormat
import pandas as pd
import numpy as np

from pandas.testing import assert_frame_equal


def test_basic_roundtrip(lmdb_version_store_v1):
    lib = lmdb_version_store_v1
    df = pd.DataFrame({"x": np.arange(10)})
    lib.write("arrow", df)
    vit = lib.read("arrow", output_format=OutputFormat.ARROW)
    result = vit.to_pandas()
    print(vit)
    assert_frame_equal(result, df)


def test_double_columns_roundtrip(lmdb_version_store_v1):
    lib = lmdb_version_store_v1
    df = pd.DataFrame({"x": np.arange(10), "y": np.arange(10.0, 20.0)})
    lib.write("arrow", df)
    vit = lib.read("arrow", output_format=OutputFormat.ARROW)
    result = vit.to_pandas()
    print(vit)
    assert_frame_equal(result, df)


def test_strings_roundtrip(lmdb_version_store_v1):
    lib = lmdb_version_store_v1
    df = pd.DataFrame({"x": ["mene", "mene", "tekel", "upharsin"]})
    lib.write("arrow", df)
    vit = lib.read("arrow", output_format=OutputFormat.ARROW)
    result = vit.to_pandas()
    print(vit)
    assert_frame_equal(result, df)


def test_date_range(lmdb_version_store_v1):
    lib = lmdb_version_store_v1
    initial_timestamp = pd.Timestamp("2019-01-01")
    df = pd.DataFrame(data=np.arange(100), index=pd.date_range(initial_timestamp, periods=100), columns=['x'])
    sym = "arrow_date_test"
    lib.write(sym, df)
    start_offset = 2
    end_offset = 5

    query_start_ts = initial_timestamp + pd.DateOffset(start_offset)
    query_end_ts = initial_timestamp + pd.DateOffset(end_offset)

    date_range = (query_start_ts, query_end_ts)
    data_closed_table = lib.read(sym, date_range=date_range, output_format=OutputFormat.ARROW)
    df = data_closed_table.to_pandas()
    df = df.set_index('index')
    assert query_start_ts == pd.Timestamp(df.index[0])
    assert query_end_ts == pd.Timestamp(df.index[-1])
    assert df['x'].iloc[0] == start_offset
    assert df['x'].iloc[-1] == end_offset
