/*
 * Copyright (c) 2017 Mellanox Technologies LTD. All rights reserved.
 *
 * This software is available to you under the terms of the
 * OpenIB.org BSD license included below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include "ibis.h"

template <class SectionRecord>
ParseFieldInfo<SectionRecord>::ParseFieldInfo(
        const char *field_name,
        bool (SectionRecord::*p_setter_func)(const char *)):
        m_field_name(field_name), m_p_setter_func(p_setter_func)
{
}

template <class SectionRecord>
SectionParser<SectionRecord>::SectionParser()
{
}

template <class SectionRecord>
SectionParser<SectionRecord>::~SectionParser()
{
    m_parse_section_info.clear();
    m_section_data.clear();
}

template <class SectionRecord>
void SectionParser<SectionRecord>::ClearData()
{
    m_section_data.clear();
}

template <class SectionRecord>
int CsvParser::ParseSection(SectionParser <SectionRecord> &section_parser)
{
    int rc = 0;
    bool field_found = false;
    ifstream f_csv;
    char line_buff[LINE_BUFF_SIZE] = {0};
    vec_str_t line_tokens;

    f_csv.exceptions(ifstream::failbit | ifstream::badbit);
    try {
        f_csv.open(this->m_csv_file.c_str());
    } catch (const ifstream::failure& e) {
        CSV_LOG(TT_LOG_LEVEL_ERROR,
                "-E- Failed to open file %s, error: %s\n",
                this->m_csv_file.c_str(), e.what());
        return 1;
    }

    const map_str_to_offset_t::iterator it =
        m_section_name_to_offset.find(section_parser.GetSectionName());

    if (it == m_section_name_to_offset.end()) {
        CSV_LOG(TT_LOG_LEVEL_ERROR,
                "-E- Failed to find section name :%s\n",
                section_parser.GetSectionName().c_str());
        return 1;
    }

    struct offset_info off_info = (*it).second;
    try {
        f_csv.seekg(off_info.offset);
    } catch (const ifstream::failure& e) {
        CSV_LOG(TT_LOG_LEVEL_ERROR,
                "-E- Failed to find section in file %s, error: %s\n",
                this->m_csv_file.c_str(), e.what());
        f_csv.close();
        return 1;
    }

    //Get the fields names
    rc= GetNextLineAndSplitIntoTokens(f_csv, line_buff, line_tokens);
    u_int16_t number_of_fields = line_tokens.size();

    vec_uint8_t vec_fields_location(section_parser.GetParseSectionInfo().size());

    for(unsigned int i = 0; i < section_parser.GetParseSectionInfo().size(); i++) {

        field_found = false;
        for (unsigned int field_location = 0; field_location < line_tokens.size();
                field_location++) {

            if (!strcmp(line_tokens[field_location],
                        (section_parser.GetParseSectionInfo()[i]).GetFieldName().c_str())) {
                vec_fields_location[i] = field_location;
                field_found = true;
                break;
            }
        }
        if (!field_found) {
            CSV_LOG(TT_LOG_LEVEL_ERROR,
                    "-E- Failed to find field %s for line number %d. Line is:%s\n",
                    section_parser.GetParseSectionInfo()[i].GetFieldName().c_str(),
                    off_info.start_line,
                    line_buff);
            f_csv.close();
            return 1;
        }
    }

    u_int32_t current_row_number = off_info.start_line;
    try {
        while ((unsigned int)f_csv.tellg() < (off_info.offset + off_info.length) &&
               f_csv.good()) {
            current_row_number++;
            rc = GetNextLineAndSplitIntoTokens(f_csv, line_buff, line_tokens);
            if (rc) {
                CSV_LOG(TT_LOG_LEVEL_ERROR,
                        "-E- CSV Parser: Failed to parse line %d"
                        " for section %s\n",
                        current_row_number, section_parser.GetSectionName().c_str());
                continue;
            }
            if (number_of_fields != line_tokens.size()) {
                CSV_LOG(TT_LOG_LEVEL_ERROR,
                        "-E- CSV Parser: number of fields in line %d doesn't"
                        " match the number of fields in this section\n",
                        current_row_number);
                continue;
            }

            SectionRecord curr_record;
            for (unsigned int fi = 0; fi < vec_fields_location.size(); fi++) {
                try {
                    (curr_record.*((section_parser.GetParseSectionInfo()[fi]).GetSetterFunc()))(
                            line_tokens[vec_fields_location[fi]]);
                } catch (TypeParseError &e) {
                    switch (e.GetReason()) {
                    case TypeParseError::EMPTY_VALUE:
                        CSV_LOG(TT_LOG_LEVEL_ERROR,
                            "-E- CSV Parser: Found empty value for field name %s, line %d"
                            " in section %s\n",
                            (section_parser.GetParseSectionInfo()[fi]).GetFieldName().c_str(),
                            current_row_number, section_parser.GetSectionName().c_str());
                    case TypeParseError::TOO_LONG_VALUE:
                        CSV_LOG(TT_LOG_LEVEL_ERROR,
                            "-E- CSV Parser: Found invalid size (over 64 bytes) value for field"
                            " name %s, line %d in section %s\n",
                            (section_parser.GetParseSectionInfo()[fi]).GetFieldName().c_str(),
                            current_row_number, section_parser.GetSectionName().c_str());
                    default:
                        CSV_LOG(TT_LOG_LEVEL_ERROR,
                            "-E- CSV Parser: Unknown exception for field name %s, line %d"
                            " in section %s\n",
                            (section_parser.GetParseSectionInfo()[fi]).GetFieldName().c_str(),
                            current_row_number, section_parser.GetSectionName().c_str());
                    }
                    f_csv.close();
                    return 1;
                }
            }

            section_parser.InsertSectionData(curr_record);
        }
    } catch (const ifstream::failure& e) {
        CSV_LOG(TT_LOG_LEVEL_ERROR,
                "-E- Failed to get info from file %s, error: %s\n",
                this->m_csv_file.c_str(), e.what());
        f_csv.close();
        return 1;
    }

    f_csv.close();
    return(rc);
}

template <class SectionRecord, class Handler>
int CsvParser::ParseAndHandleSection(
        SectionParser <SectionRecord> &section_parser,
        Handler *p_handler,
        int (Handler::*handler_func)(const SectionRecord &)) {

    if (m_section_name_to_offset.empty())
        UpdateSectionOffsetTable();

    int rc = ParseSection(section_parser);
    if (rc)
        return (1);
    for (unsigned int i = 0;
         i < section_parser.GetSectionData().size();
         i++)
        (p_handler->*handler_func)(section_parser.GetSectionData()[i]);

    section_parser.ClearData();
    return 0;
}

