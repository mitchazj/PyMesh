/* This file is part of PyMesh. Copyright (c) 2015 by Qingnan Zhou */
#include "MshLoader.h"

#include <iostream>
#include <sstream>
#include <vector>

#include <Core/Exception.h>

MshLoader::MshLoader(const std::string& filename) {
    std::ifstream fin(filename.c_str(), std::ios::in | std::ios::binary);

    if (!fin.is_open()) {
        std::stringstream err_msg;
        err_msg << "failed to open file \"" << filename << "\"";
        throw IOError(err_msg.str());
    }
    // Parse header
    std::string buf;
    double version;
    int type;
    fin >> buf;
    if (buf != "$MeshFormat") { throw INVALID_FORMAT; }

    fin >> version >> type >> m_data_size;
    m_binary = (type == 1);

    // Some sanity check.
    if (m_data_size != 8) {
        std::cerr << "Error: data size must be 8 bytes." << std::endl;
        throw NOT_IMPLEMENTED;
    }
    if (sizeof(int) != 4) {
        std::cerr << "Error: code must be compiled with int size 4 bytes." << std::endl;
        throw NOT_IMPLEMENTED;
    }

    // Read in extra info from binary header.
    if (m_binary) {
        int one;
        eat_white_space(fin);
        fin.read(reinterpret_cast<char*>(&one), sizeof(int));
        if (one != 1) {
            std::cerr << "Warning: binary msh file " << filename
                << " is saved with different endianness than this machine."
                << std::endl;
            throw NOT_IMPLEMENTED;
        }
    }

    fin >> buf;
    if (buf != "$EndMeshFormat") { throw NOT_IMPLEMENTED; }

    while (!fin.eof()) {
        buf.clear();
        fin >> buf;
        if (buf == "$Nodes") {
            parse_nodes(fin);
            fin >> buf;
            if (buf != "$EndNodes") { throw INVALID_FORMAT; }
        } else if (buf == "$Elements") {
            parse_elements(fin);
            fin >> buf;
            if (buf != "$EndElements") { throw INVALID_FORMAT; }
        } else if (buf == "$NodeData") {
            parse_node_field(fin);
            fin >> buf;
            if (buf != "$EndNodeData") { throw INVALID_FORMAT; }
        } else if (buf == "$ElementData") {
            parse_element_field(fin);
            fin >> buf;
            if (buf != "$EndElementData") { throw INVALID_FORMAT; }
        } else if (fin.eof()) {
            break;
        } else {
            parse_unknown_field(fin, buf);
        }
    }
    fin.close();
}

MshLoader::FieldNames MshLoader::get_node_field_names() const {
    FieldNames result;

    for (FieldMap::const_iterator itr = m_node_fields.begin();
            itr != m_node_fields.end(); itr++) {
        result.push_back(itr->first);
    }
    return result;
}

MshLoader::FieldNames MshLoader::get_element_field_names() const {
    FieldNames result;

    for (FieldMap::const_iterator itr = m_element_fields.begin();
            itr != m_element_fields.end(); itr++) {
        result.push_back(itr->first);
    }
    return result;
}

void MshLoader::parse_nodes(std::ifstream& fin) {
    size_t num_nodes;
    fin >> num_nodes;
    m_nodes.resize(num_nodes*3);

    if (m_binary) {
        size_t num_bytes = (4+3*m_data_size) * num_nodes;
        char* data = new char[num_bytes];
        eat_white_space(fin);
        fin.read(data, num_bytes);

        for (size_t i=0; i<num_nodes; i++) {
            int node_idx          = *reinterpret_cast<int*>  (&data[i*(4+3*m_data_size)]) - 1;
            m_nodes[node_idx*3]   = *reinterpret_cast<Float*>(&data[i*(4+3*m_data_size) + 4]);
            m_nodes[node_idx*3+1] = *reinterpret_cast<Float*>(&data[i*(4+3*m_data_size) + 4 + m_data_size]);
            m_nodes[node_idx*3+2] = *reinterpret_cast<Float*>(&data[i*(4+3*m_data_size) + 4 + 2*m_data_size]);
        }

        delete [] data;
    } else {
        int node_idx;
        for (size_t i=0; i<num_nodes; i++) {
            fin >> node_idx;
            node_idx -= 1;
            fin >> m_nodes[node_idx*3]
                >> m_nodes[node_idx*3+1]
                >> m_nodes[node_idx*3+2];
        }
    }
    if (!m_nodes.allFinite()) {
        throw IOError("NaN or Inf detected in input file.");
    }
}

void MshLoader::parse_elements(std::ifstream& fin) {
    size_t num_elements;
    fin >> num_elements;

    // Tmp storage of elements;
    std::vector<int> elements;
    std::vector<int> element_idx;
    size_t nodes_per_element;
    int glob_elem_type = -1;

    if (m_binary) {
        eat_white_space(fin);
        int elem_read = 0;
        while (elem_read < num_elements) {
            // Parse element header.
            int elem_type, num_elems, num_tags;
            fin.read((char*)&elem_type, sizeof(int));
            fin.read((char*)&num_elems, sizeof(int));
            fin.read((char*)&num_tags, sizeof(int));
            nodes_per_element = num_nodes_per_elem_type(elem_type);

            // check for element type consistency.
            if (glob_elem_type == -1) {
                glob_elem_type = elem_type;
            } else if (glob_elem_type != elem_type) {
                std::cerr << "Error: all elements must have the same type." << std::endl;
                throw NOT_IMPLEMENTED;
                return;
            }

            for (size_t i=0; i<num_elems; i++) {
                int elem_idx;
                fin.read((char*)&elem_idx, sizeof(int));
                elem_idx -= 1;
                element_idx.push_back(elem_idx);

                // Eat up tags.
                for (size_t j=0; j<num_tags; j++) {
                    int tag;
                    fin.read((char*)&tag, sizeof(int));
                }

                // Element values.
                for (size_t j=0; j<nodes_per_element; j++) {
                    int idx;
                    fin.read((char*)&idx, sizeof(int));
                    elements.push_back(idx-1);
                }
            }

            elem_read += num_elems;
        }
    } else {
        for (size_t i=0; i<num_elements; i++) {
            // Parse per element header
            int elem_num, elem_type, num_tags;
            fin >> elem_num >> elem_type >> num_tags;
            for (size_t j=0; j<num_tags; j++) {
                int tag;
                fin >> tag;
            }
            nodes_per_element = num_nodes_per_elem_type(elem_type);

            elem_num -= 1;
            element_idx.push_back(elem_num);

            // check for element type consistency.
            if (glob_elem_type == -1) {
                glob_elem_type = elem_type;
            } else if (glob_elem_type != elem_type) {
                std::cerr << "Error: all elements must have the same type." << std::endl;
                throw NOT_IMPLEMENTED;
                return;
            }

            // Parse node idx.
            for (size_t j=0; j<nodes_per_element; j++) {
                int idx;
                fin >> idx;
                elements.push_back(idx-1); // msh index starts from 1.
            }
        }
    }

    // Copy to m_element.
    m_elements.resize(elements.size());
    for (size_t i=0; i<element_idx.size(); i++) {
        for (size_t j=0; j<nodes_per_element; j++) {
            m_elements[element_idx[i]*nodes_per_element+j] =
                elements[i*nodes_per_element+j];
        }
    }

    m_nodes_per_element = nodes_per_element;
    if (glob_elem_type != -1) {
        m_element_type = glob_elem_type;
    } else if (m_elements.rows() == 0) {
        // No elements specified, use triangle as element type.
        m_element_type = 2;
    } else {
        throw INVALID_FORMAT;
    }
}

void MshLoader::parse_node_field(std::ifstream& fin) {
    size_t num_string_tags;
    size_t num_real_tags;
    size_t num_int_tags;

    fin >> num_string_tags;
    std::string* str_tags = new std::string[num_string_tags];
    for (size_t i=0; i<num_string_tags; i++) {
        eat_white_space(fin);
        if (fin.peek() == '\"') {
            // Handle field name between quoates.
            char buf[128];
            fin.get(); // remove the quote at the beginning.
            fin.getline(buf, 128, '\"');
            str_tags[i] = std::string(buf);
        } else {
            fin >> str_tags[i];
        }
    }

    fin >> num_real_tags;
    Float* real_tags = new Float[num_real_tags];
    for (size_t i=0; i<num_real_tags; i++)
        fin >> real_tags[i];

    fin >> num_int_tags;
    int* int_tags = new int[num_int_tags];
    for (size_t i=0; i<num_int_tags; i++)
        fin >> int_tags[i];

    if (num_string_tags <= 0 || num_int_tags <= 2) { throw INVALID_FORMAT; }
    std::string fieldname = str_tags[0];
    int num_components = int_tags[1];
    int num_entries = int_tags[2];
    VectorF field(num_entries * num_components);

    delete [] str_tags;
    delete [] real_tags;
    delete [] int_tags;

    if (m_binary) {
        size_t num_bytes = (num_components * m_data_size + 4) * num_entries;
        char* data = new char[num_bytes];
        eat_white_space(fin);
        fin.read(data, num_bytes);
        for (size_t i=0; i<num_entries; i++) {
            int node_idx = *reinterpret_cast<int*>(&data[i*(4+num_components*m_data_size)]);
            node_idx -= 1;
            size_t base_idx = i*(4+num_components*m_data_size) + 4;
            for (size_t j=0; j<num_components; j++) {
                field[node_idx * num_components + j] = *reinterpret_cast<Float*>(&data[base_idx+j*m_data_size]);
            }
        }
        delete [] data;
    } else {
        int node_idx;
        for (size_t i=0; i<num_entries; i++) {
            fin >> node_idx;
            node_idx -= 1;
            for (size_t j=0; j<num_components; j++) {
                fin >> field[node_idx * num_components + j];
            }
        }
    }

    m_node_fields[fieldname] = field;
}

void MshLoader::parse_element_field(std::ifstream& fin) {
    size_t num_string_tags;
    size_t num_real_tags;
    size_t num_int_tags;

    fin >> num_string_tags;
    std::string* str_tags = new std::string[num_string_tags];
    for (size_t i=0; i<num_string_tags; i++) {
        eat_white_space(fin);
        if (fin.peek() == '\"') {
            // Handle field name between quoates.
            char buf[128];
            fin.get(); // remove the quote at the beginning.
            fin.getline(buf, 128, '\"');
            str_tags[i] = std::string(buf);
        } else {
            fin >> str_tags[i];
        }
    }

    fin >> num_real_tags;
    Float* real_tags = new Float[num_real_tags];
    for (size_t i=0; i<num_real_tags; i++)
        fin >> real_tags[i];

    fin >> num_int_tags;
    int* int_tags = new int[num_int_tags];
    for (size_t i=0; i<num_int_tags; i++)
        fin >> int_tags[i];

    if (num_string_tags <= 0 || num_int_tags <= 2) { throw INVALID_FORMAT; }
    std::string fieldname = str_tags[0];
    int num_components = int_tags[1];
    int num_entries = int_tags[2];
    VectorF field(num_entries * num_components);

    delete [] str_tags;
    delete [] real_tags;
    delete [] int_tags;

    if (m_binary) {
        size_t num_bytes = (num_components * m_data_size + 4) * num_entries;
        char* data = new char[num_bytes];
        eat_white_space(fin);
        fin.read(data, num_bytes);
        for (size_t i=0; i<num_entries; i++) {
            int elem_idx = *reinterpret_cast<int*>(&data[i*(4+num_components*m_data_size)]);
            elem_idx -= 1;
            size_t base_idx = i*(4+num_components*m_data_size) + 4;
            for (size_t j=0; j<num_components; j++) {
                field[elem_idx * num_components + j] = *reinterpret_cast<Float*>(&data[base_idx+j*m_data_size]);
            }
        }
        delete [] data;
    } else {
        int elem_idx;
        for (size_t i=0; i<num_entries; i++) {
            fin >> elem_idx;
            elem_idx -= 1;
            for (size_t j=0; j<num_components; j++) {
                fin >> field[elem_idx * num_components + j];
            }
        }
    }

    m_element_fields[fieldname] = field;
}

void MshLoader::parse_unknown_field(std::ifstream& fin,
        const std::string& fieldname) {
    std::cerr << "Warning: \"" << fieldname << "\" not supported yet.  Ignored." << std::endl;
    std::string endmark = fieldname.substr(0,1) + "End"
        + fieldname.substr(1,fieldname.size()-1);

    std::string buf("");
    while (buf != endmark && !fin.eof()) {
        fin >> buf;
    }
}

void MshLoader::eat_white_space(std::ifstream& fin) {
    char next = fin.peek();
    while (next == '\n' || next == ' ' || next == '\t') {
        fin.get();
        next = fin.peek();
    }
}

int MshLoader::num_nodes_per_elem_type(int elem_type) {
    size_t nodes_per_element = 0;
    switch (elem_type) {
        case 2:
            nodes_per_element = 3; // Triangle
            break;
        case 3:
            nodes_per_element = 4; // Quad
            break;
        case 4:
            nodes_per_element = 4; // Tet
            break;
        case 5:
            nodes_per_element = 8; // hexahedron
            break;
        default:
            std::cerr << "Warning: Element type (" << elem_type << ") is not supported yet."
                << std::endl;
            throw NOT_IMPLEMENTED;
    }
    return nodes_per_element;
}
