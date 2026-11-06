#!/usr/bin/env python3
"""Verifies the dbPath layout rules implemented in TtsDirect.prepareDbPath.

The engine (libvt_custom12_*.so / libvt_custom15_*.so) concatenates these literal
fragments onto the dbPath it is handed:
    "fixed_db/"            + tts_single_db_kor_custom<tag>_<variant>.vtdb
    "%sdata-common/"       + <common db files>
    "userdict/userdict_kor.csv"

This script mirrors the Kotlin classification logic and asserts that a flat voice
pack folder is mapped to exactly the tree the engine will look for.

Run:  python3 tests/test_dbpath_layout.py
"""
import sys

# --- mirror of TtsDirect constants -------------------------------------------
DIR_DATA_COMMON = "data-common"
DIR_FIXED_DB = "fixed_db"
DIR_USERDICT = "userdict"
USERDICT_FILE = "userdict_kor.csv"
LIB_VARIANTS = ("H22", "sP22")


def vtdb_name(tag, variant):
    return f"tts_single_db_kor_custom{tag}_{variant}.vtdb"


def is_fixed_db(name):
    return name.startswith("tts_single_db_") and name.endswith(".vtdb")


def stage_dir_name(tag):
    """Mirror of the staged tree name used by prepareDbPath."""
    return f"staged{tag}"


def classify(name, tag):
    """Where prepareDbPath files a source file. tag is unused for the rule itself."""
    if is_fixed_db(name):
        return DIR_FIXED_DB
    if name == USERDICT_FILE:
        return DIR_USERDICT
    return DIR_DATA_COMMON


def stage(src_names, tag):
    """Returns the staged tree as {relpath: source_name}."""
    out = {}
    for name in src_names:
        d = classify(name, tag)
        out[f"{d}/{name}"] = name
    return out


def main():
    # A realistic flat CUSTOM12 pack, as it ships in hdtts/custom12/.
    flat12 = [
        "tts_single_db_kor_custom12_H22.vtdb",
        "tts_single_db.vtdb2",
        "userdict_kor.csv",
        "Diclist_Custom12.txt",
        "HMC_TUNED_CORPUS.cps",
        "HMC_POI_CORPUS.poi",
        "hts_for_navi_cityname.txt",
        "public_sentence_modify.txt",
    ]
    tree = stage(flat12, "12")

    # The three paths the engine builds must all exist after staging.
    assert f"{DIR_FIXED_DB}/{vtdb_name('12', 'H22')}" in tree, tree
    assert f"{DIR_USERDICT}/{USERDICT_FILE}" in tree, tree
    assert f"{DIR_DATA_COMMON}/Diclist_Custom12.txt" in tree, tree
    assert f"{DIR_DATA_COMMON}/tts_single_db.vtdb2" in tree, tree

    # The speaker model must NOT land in data-common, and the userdict must not
    # sit at the dbPath root (the engine reads userdict/userdict_kor.csv).
    assert f"{DIR_DATA_COMMON}/{vtdb_name('12', 'H22')}" not in tree
    assert USERDICT_FILE not in tree

    # A CUSTOM15 pack whose only speaker model is the sP22 variant must still be
    # recognised as the fixed DB (the old suffix match only accepted H22).
    flat15 = [
        "tts_single_db_kor_custom15_sP22.vtdb",
        "userdict_kor.csv",
        "Diclist_Custom15.txt",
    ]
    tree15 = stage(flat15, "15")
    assert f"{DIR_FIXED_DB}/{vtdb_name('15', 'sP22')}" in tree15, tree15
    assert f"{DIR_DATA_COMMON}/{vtdb_name('15', 'sP22')}" not in tree15

    # A pack with both variants must keep both in fixed_db.
    both = ["tts_single_db_kor_custom15_H22.vtdb",
            "tts_single_db_kor_custom15_sP22.vtdb"]
    treeb = stage(both, "15")
    assert f"{DIR_FIXED_DB}/tts_single_db_kor_custom15_H22.vtdb" in treeb
    assert f"{DIR_FIXED_DB}/tts_single_db_kor_custom15_sP22.vtdb" in treeb

    # open() pairs a library with a DB of the same variant, so each variant must
    # be able to find its own file.
    for variant in LIB_VARIANTS:
        assert f"{DIR_FIXED_DB}/{vtdb_name('12', variant)}" in stage(
            [vtdb_name("12", variant)], "12")

    # The vtdb2 common DB is large and must be classified as a common file, not
    # mistaken for the fixed DB (it has no "tts_single_db_" prefix + .vtdb suffix).
    assert not is_fixed_db("tts_single_db.vtdb2")
    assert is_fixed_db("tts_single_db_kor_custom12_H22.vtdb")

    # The staged tree must not collide with the source pack folder, which is
    # custom{tag}/ directly under the hdtts root.
    assert stage_dir_name("12") != "custom12"
    assert stage_dir_name("15") != "custom15"

    # Both speakers must be stageable from the same root; their files differ only
    # by tag and must not share a staged directory.
    assert stage_dir_name("12") != stage_dir_name("15")

    # Exact file list shipped in hdtts.zip, for both speakers.
    hdtts12 = [
        "Diclist_Custom12.txt", "Diclist_Custom12KAKAOHTS.txt",
        "Diclist_Custom12LOCATION.txt", "Diclist_Custom12NAME.txt",
        "Diclist_Custom12PHONE.txt", "Diclist_Custom12SMS.txt",
        "HMC_LOCATION_CORPUS.cps", "HMC_POI_CORPUS.poi", "HMC_TUNED_CORPUS.cps",
        "hts_for_navi_cityname.txt", "public_sentence_modify.txt",
        "tts_single_db.vtdb2", "tts_single_db_kor_custom12_H22.vtdb",
        "userdict_kor.csv",
    ]
    t12 = stage(hdtts12, "12")
    assert f"{DIR_FIXED_DB}/tts_single_db_kor_custom12_H22.vtdb" in t12
    assert f"{DIR_USERDICT}/userdict_kor.csv" in t12
    assert f"{DIR_DATA_COMMON}/tts_single_db.vtdb2" in t12
    assert len(t12) == len(hdtts12), "every file must be staged exactly once"

    hdtts15 = [
        "Diclist_Custom15.txt", "Diclist_Custom15KAKAO.txt",
        "Diclist_Custom15KAKAOHTS.txt", "Diclist_Custom15LOCATION.txt",
        "Diclist_Custom15NAME.txt", "Diclist_Custom15PHONE.txt",
        "Diclist_Custom15SMS.txt", "HMC_LOCATION_CORPUS.cps",
        "HMC_POI_CORPUS.poi", "HMC_TUNED_CORPUS.cps",
        "hts_for_navi_cityname.txt", "public_sentence_modify.txt",
        "tts_single_db.vtdb2", "tts_single_db_kor_custom15_H22.vtdb",
        "userdict_kor.csv", "userdict_kor_HIGH_USS.csv",
    ]
    t15 = stage(hdtts15, "15")
    assert f"{DIR_FIXED_DB}/tts_single_db_kor_custom15_H22.vtdb" in t15
    assert f"{DIR_USERDICT}/userdict_kor.csv" in t15
    assert f"{DIR_DATA_COMMON}/tts_single_db.vtdb2" in t15
    assert f"{DIR_DATA_COMMON}/userdict_kor_HIGH_USS.csv" in t15
    assert len(t15) == len(hdtts15), "every file must be staged exactly once"

    print("all dbPath layout tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
