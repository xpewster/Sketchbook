#include <pugixml.hpp>
#include <string>
#include <unordered_map>
#include <iostream>

void parseXMLNode(const pugi::xml_node& node, const std::string& path,
                  std::unordered_map<std::string, std::string>& params) {
    if (!node) return;

    // Build current path including the element name
    std::string currentPath = path.empty() ? std::string(node.name())
                                            : path + "." + node.name();

    // Append attributes to path (e.g., "book[id=bk101]")
    for (pugi::xml_attribute attr : node.attributes()) {
        currentPath += "[" + std::string(attr.name()) + "=" + attr.value() + "]";
    }

    // Find the first *element* child.
    pugi::xml_node firstElementChild = node.first_child();
    while (firstElementChild && firstElementChild.type() != pugi::node_element) {
        firstElementChild = firstElementChild.next_sibling();
    }

    // If this element has text content (and no child elements), store it
    if (!firstElementChild && !node.text().empty()) {
        params[currentPath] = node.text().get();
    }

    // Recurse into child elements
    for (pugi::xml_node child = firstElementChild; child; child = child.next_sibling()) {
        if (child.type() != pugi::node_element) continue;
        parseXMLNode(child, currentPath, params);
    }
}

int parseXMLFile(const std::string& filePath,
                 std::unordered_map<std::string, std::string>& params) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(filePath.c_str());
    if (!result) {
        LOG_WARN << "Failed to load XML file: " << filePath
                 << " (" << result.description() << ")\n";
        return 1;
    }

    pugi::xml_node root = doc.document_element();
    if (root) {
        parseXMLNode(root, "", params);
    }
    return 0;
}
