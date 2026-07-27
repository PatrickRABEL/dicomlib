#!/usr/bin/env python3
"""Generate DICOM dictionary and UID tables from official DocBook XML."""

import argparse
import re
import textwrap
import xml.etree.ElementTree as ET


DB = {"db": "http://docbook.org/ns/docbook"}
XML_ID = "{http://www.w3.org/XML/1998/namespace}id"


def clean_text(value):
    return " ".join(value.replace("\u200b", "").split())


def cpp_string(value):
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def table(root, table_id):
    return root.find(".//db:table[@%s='%s']" % (XML_ID, table_id), DB)


def rows(root, table_id):
    t = table(root, table_id)
    if t is None:
        raise RuntimeError("table not found: %s" % table_id)
    for row in t.findall(".//db:tbody/db:tr", DB):
        yield [clean_text("".join(td.itertext())) for td in row.findall("db:td", DB)]


def vr_token(value):
    tokens = clean_text(value).split()
    if not tokens:
        return "UN"
    token = tokens[0]
    if token == "See":
        return "UN"
    return token


def tag_pattern(tag):
    m = re.match(r"\(([0-9A-Fa-fxX]{4}),([0-9A-Fa-fxX]{4})\)", tag)
    if not m:
        raise ValueError("unsupported tag syntax: %s" % tag)
    raw = (m.group(1) + m.group(2)).upper()
    value = 0
    mask = 0
    for ch in raw:
        value <<= 4
        mask <<= 4
        if ch == "X":
            continue
        value |= int(ch, 16)
        mask |= 0xF
    return value, mask


def dictionary_entries(part06, part07):
    result = {}
    for row in rows(part07, "table_E.1-1"):
        value, mask = tag_pattern(row[0])
        result[(value, mask)] = (vr_token(row[3]), row[2])
    for row in rows(part07, "table_E.2-1"):
        value, mask = tag_pattern(row[0])
        result[(value, mask)] = (vr_token(row[3]), row[2])
    for table_id in ("table_7-1", "table_8-1", "table_6-1"):
        for row in rows(part06, table_id):
            if len(row) < 4 or not row[2] or not row[3]:
                continue
            value, mask = tag_pattern(row[0])
            result[(value, mask)] = (vr_token(row[3]), row[2])

    # Retired group length elements remain common in legacy files.
    result.setdefault((0x00000000, 0x0000FFFF), ("UL", "GroupLength"))
    return sorted((value, mask, vr, name) for (value, mask), (vr, name) in result.items())


def uid_entries(part06):
    result = {}
    for table_id in ("table_A-1", "table_A-2", "table_A-3", "table_A-4"):
        for row in rows(part06, table_id):
            uid = clean_text(row[0])
            if not re.match(r"^[0-9.]+$", uid):
                continue
            result[uid] = (row[1], row[3] if len(row) > 3 else "")
    return sorted((uid, name, uid_type) for uid, (name, uid_type) in result.items())


def generated_header(source):
    return textwrap.dedent("""\
        /************************************************************************
        *   DICOMLIB
        *   Copyright 2003 Sunnybrook and Women's College Health Science Center
        *   Implemented by Trevor Morgan  (morgan@sten.sunnybrook.utoronto.ca)
        *
        *   See LICENSE.txt for copyright and licensing info.
        *************************************************************************/

        /*
            Generated from the official DICOM {source} DocBook XML.
            Do not edit dictionary entries by hand; run tools/generate_dicom_standard_tables.py.
        */

        """).format(source=source)


def write_dictionary(path, entries, source):
    exact = [e for e in entries if e[1] == 0xFFFFFFFF]
    wild = [e for e in entries if e[1] != 0xFFFFFFFF]
    with open(path, "w") as out:
        out.write(generated_header(source))
        out.write(textwrap.dedent("""\
            #include "DataDictionary.hpp"
            #include <algorithm>
            #include <sstream>
            #include "VR.hpp"
            #include "Tag.hpp"
            #include <iomanip>
            namespace dicom
            {
                namespace
                {
                    struct DictionaryEntry
                    {
                        UINT32 tag;
                        VR vr;
                        const char * name;
                    };

                    struct WildcardDictionaryEntry
                    {
                        UINT32 tag;
                        UINT32 mask;
                        VR vr;
                        const char * name;
                    };

                    static const DictionaryEntry DICT_ENTRIES[] =
                    {
            """))
        for value, mask, vr, name in exact:
            out.write("\t\t\t{0x%08X,VR_%s,%s},\n" % (value, vr, cpp_string(name)))
        out.write(textwrap.dedent("""\
                    };

                    static const WildcardDictionaryEntry WILDCARD_DICT_ENTRIES[] =
                    {
            """))
        for value, mask, vr, name in wild:
            out.write("\t\t\t{0x%08X,0x%08X,VR_%s,%s},\n" % (value, mask, vr, cpp_string(name)))
        out.write(textwrap.dedent("""\
                    };

                    typedef std::pair<VR, std::string> Item;

                    std::pair<Tag,Item>
                    MakeValueType(const DictionaryEntry& entry)
                    {
                        return std::pair<Tag,Item>(Tag(entry.tag), Item(entry.vr,entry.name));
                    }

                    const WildcardDictionaryEntry*
                    FindWildcardEntry(Tag tag)
                    {
                        UINT32 value = UINT32(tag);
                        for(size_t i=0;i<sizeof(WILDCARD_DICT_ENTRIES)/sizeof(WildcardDictionaryEntry);++i)
                        {
                            const WildcardDictionaryEntry& entry = WILDCARD_DICT_ENTRIES[i];
                            if((value & entry.mask) == entry.tag)
                                return &entry;
                        }
                        return 0;
                    }

                    struct DataDictionary : std::map<Tag,Item>
                    {
                        DataDictionary()
                        {
                            std::transform( DICT_ENTRIES,
                                            DICT_ENTRIES+sizeof(DICT_ENTRIES)/sizeof(DictionaryEntry),
                                            std::inserter(*this, begin()),
                                            MakeValueType);
                        }

                        VR GetVR(Tag tag)
                        {
                            iterator I = find(tag);
                            if(I!=end())
                                return I->second.first;

                            const WildcardDictionaryEntry* wildcard = FindWildcardEntry(tag);
                            if(wildcard)
                                return wildcard->vr;

                            return VR_UN;
                        }
                        std::string GetName(Tag tag)
                        {
                            iterator I = find(tag);
                            if(I!=end())
                                return I->second.second;

                            const WildcardDictionaryEntry* wildcard = FindWildcardEntry(tag);
                            if(wildcard)
                                return wildcard->name;

                            std::ostringstream os;
                            os << "(" << GroupTag(tag) << "," << ElementTag(tag) << ")";
                            return os.str();
                        }
                        std::string GetTagString(Tag tag)
                        {
                            std::ostringstream os;
                            os << "(" << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << GroupTag(tag) << "," << std::setw(4) << std::setfill('0') << ElementTag(tag) << ")";
                            return os.str();
                        }
                    };

                    DataDictionary TheDataDictionary;

                }//anonymous namespace

                VR GetVR(Tag tag)
                {
                    return TheDataDictionary.GetVR(tag);
                }

                std::string GetName(Tag tag)
                {
                    return TheDataDictionary.GetName(tag);
                }

                std::string GetTagString(Tag tag)
                {
                    return TheDataDictionary.GetTagString(tag);
                }

                void AddDictionaryEntry(Tag tag, VR vr, std::string name)
                {
                    Enforce(TheDataDictionary.end()==TheDataDictionary.find(tag),"Item already exists");
                    UINT16 group=GroupTag(tag);
                    Enforce(group&0x01,"Group element must be odd.");
                    TheDataDictionary[tag]=Item(vr,name);
                }
            }//namespace dicom
            """))


def is_encapsulated(uid, name):
    if uid in (
        "1.2.840.10008.1.2",
        "1.2.840.10008.1.2.1",
        "1.2.840.10008.1.2.1.99",
        "1.2.840.10008.1.2.2",
    ):
        return False
    return uid.startswith("1.2.840.10008.1.2.")


def write_uids(path, entries, source):
    transfer = [(uid, name) for uid, name, uid_type in entries if uid_type == "Transfer Syntax"]
    encapsulated = [(uid, name) for uid, name in transfer if is_encapsulated(uid, name)]
    with open(path, "w") as out:
        out.write(generated_header(source))
        out.write(textwrap.dedent("""\
            #include "UIDs.hpp"
            #include<map>
            using namespace std;
            namespace dicom
            {
                namespace
                {
                    struct UIDEntry
                    {
                        const char* uid;
                        const char* name;
                    };

                    static const UIDEntry UID_ENTRIES[] =
                    {
            """))
        for uid, name, uid_type in entries:
            out.write("\t\t\t{%s,%s},\n" % (cpp_string(uid), cpp_string(name)))
        out.write(textwrap.dedent("""\
                    };

                    static const char* TRANSFER_SYNTAX_UIDS[] =
                    {
            """))
        for uid, name in transfer:
            out.write("\t\t\t%s,\n" % cpp_string(uid))
        out.write(textwrap.dedent("""\
                    };

                    static const char* ENCAPSULATED_TRANSFER_SYNTAX_UIDS[] =
                    {
            """))
        for uid, name in encapsulated:
            out.write("\t\t\t%s,\n" % cpp_string(uid))
        out.write(textwrap.dedent("""\
                    };

                    void PopulateMap(map<UID,string>& THEMAP)
                    {
                        for(size_t i=0;i<sizeof(UID_ENTRIES)/sizeof(UIDEntry);++i)
                            THEMAP[UID(UID_ENTRIES[i].uid)]=UID_ENTRIES[i].name;
                    }

                    bool ContainsUID(const char* const* begin, const char* const* end, UID uid)
                    {
                        for(const char* const* i=begin;i!=end;++i)
                        {
                            if(uid == UID(*i))
                                return true;
                        }
                        return false;
                    }

                    map<UID,string>THEMAP;
                    bool MapIsPopulated=false;
                }//namespace

                string GetUIDName(UID uid)
                {
                    if(!MapIsPopulated)
                    {
                        PopulateMap(THEMAP);
                        MapIsPopulated=true;
                    }
                    if(THEMAP.find(uid)!=THEMAP.end())
                        return THEMAP[uid];
                    else
                        return "Unknown";
                }

                bool IsTransferSyntaxUID(UID uid)
                {
                    return ContainsUID(
                        TRANSFER_SYNTAX_UIDS,
                        TRANSFER_SYNTAX_UIDS+sizeof(TRANSFER_SYNTAX_UIDS)/sizeof(const char*),
                        uid);
                }

                bool IsEncapsulatedTransferSyntaxUID(UID uid)
                {
                    return ContainsUID(
                        ENCAPSULATED_TRANSFER_SYNTAX_UIDS,
                        ENCAPSULATED_TRANSFER_SYNTAX_UIDS+sizeof(ENCAPSULATED_TRANSFER_SYNTAX_UIDS)/sizeof(const char*),
                        uid);
                }
            }
            """))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--part06", required=True)
    parser.add_argument("--part07", required=True)
    parser.add_argument("--dictionary", default="dicomlib/DataDictionary.cpp")
    parser.add_argument("--uids", default="dicomlib/UIDs.cpp")
    parser.add_argument("--source", default="2026c")
    args = parser.parse_args()

    part06 = ET.parse(args.part06).getroot()
    part07 = ET.parse(args.part07).getroot()
    write_dictionary(args.dictionary, dictionary_entries(part06, part07), args.source)
    write_uids(args.uids, uid_entries(part06), args.source)


if __name__ == "__main__":
    main()
