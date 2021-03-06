"""
Testing general loading functionality. For more information on tests refer to
same tests in dlis section
"""

import pytest

import shutil
import os

from dlisio import lis, core, common

def test_filehandles_closed(tmpdir):
    # Copy the test file to a tmpdir in order to make this test reliable.
    tmp = str(tmpdir.join('file'))
    shutil.copyfile('data/lis/layouts/layout_tif_01.lis', tmp)

    load_failure_escape = str(tmpdir.join('truncated.lis'))
    shutil.copyfile('data/lis/layouts/truncated_15.lis', load_failure_escape)

    with lis.load(tmp) as files:
        assert len(files) == 4

    errorhandler = common.ErrorHandler(
        critical=common.Actions.LOG_ERROR)
    # file is truncated, but 2 LFs were already processed successfully
    with lis.load(load_failure_escape, error_handler=errorhandler) as files:
        assert len(files) == 2

    os.remove(tmp)
    os.remove(load_failure_escape)

def test_filehandles_closed_when_load_fails(tmpdir, merge_lis_prs):
    # majority of exceptions in load are hard to invoke, so they are not tested
    empty = os.path.join(str(tmpdir), 'empty.lis')
    merge_lis_prs(empty, [])

    truncated = str(tmpdir.join('truncated.lis'))
    shutil.copyfile('data/lis/layouts/truncated_15.lis', truncated)

    openlis = str(tmpdir.join('openlis-error.lis'))
    shutil.copyfile('data/lis/layouts/wrong_06.lis', openlis)

    with pytest.raises(OSError):
        _ =  lis.load(empty)

    with pytest.raises(RuntimeError):
        _ = lis.load(truncated)

    with pytest.raises(RuntimeError):
        _ = lis.load(openlis)

    os.remove(empty)
    os.remove(truncated)
    os.remove(openlis)

def test_load_nonexisting_file():
    with pytest.raises(OSError) as exc:
        _ = lis.load("this_file_does_not_exist.lis")
    msg = "unable to open file for path this_file_does_not_exist.lis"
    assert msg in str(exc.value)

@pytest.mark.parametrize('id', [
    "中国文字",
    "ąęóćłńśźżçè",
    ])
def test_file_with_nonascii_name(tmpdir, id):
    path = str(tmpdir.join('file with '+id+'.lis'))
    shutil.copyfile('data/lis/layouts/layout_tif_01.lis', path)

    with lis.load(path) as files:
        assert len(files) == 4

def test_load_empty_file(tmpdir, merge_lis_prs):
    fpath = os.path.join(str(tmpdir), 'empty.lis')
    merge_lis_prs(fpath, [])
    with pytest.raises(OSError) as exc:
        _ = lis.load(fpath)
    assert "Cannot read 12 first bytes of file" in str(exc.value)

def test_file_partitioning(tmpdir, merge_lis_prs):
    fpath = os.path.join(str(tmpdir), 'partitioning.lis')

    content = [
        'data/lis/records/RHLR-1.lis.part',
        'data/lis/records/THLR-1.lis.part',
        'data/lis/records/FHLR-1.lis.part',
        'data/lis/records/flic-comment.lis.part',
        'data/lis/records/FTLR-1.lis.part',
        'data/lis/records/FHLR-2.lis.part',
        'data/lis/records/wellsite-data.lis.part',
        'data/lis/records/curves/dfsr-simple.lis.part',
        'data/lis/records/curves/fdata-simple.lis.part',
        'data/lis/records/FTLR-2.lis.part',
        'data/lis/records/TTLR-1.lis.part',
        'data/lis/records/RTLR-1.lis.part',
    ]
    merge_lis_prs(fpath, content)

    with lis.load(fpath) as (f1, f2, *tail):
        assert len(tail) == 0

        assert [x.type for x in f1.index.explicits()] == [
            core.lis_rectype.file_header,
            core.lis_rectype.flic_comment,
            core.lis_rectype.file_trailer,
        ]
        assert [x.type for x in f1.index.implicits()] == []
        assert [x.type for x in f2.index.explicits()] == [
            core.lis_rectype.file_header,
            core.lis_rectype.wellsite_data,
            core.lis_rectype.data_format_spec,
            core.lis_rectype.file_trailer,
        ]
        assert [x.type for x in f2.index.implicits()] == [
            core.lis_rectype.normal_data
        ]
