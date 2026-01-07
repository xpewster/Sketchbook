#include <tinyxml2.h>
#include <string>
#include <unordered_map>
#include <iostream>

using namespace tinyxml2;

void parseXMLNode(XMLElement* element, const std::string& path, 
                  std::unordered_map<std::string, std::string>& params) {
    if (!element) return;

    // Build current path including attributes
    std::string currentPath = path.empty() ? element->Name() : path + "." + element->Name();
    
    // Append attributes to path (e.g., "book[id=bk101]")
    const XMLAttribute* attr = element->FirstAttribute();
    while (attr) {
        currentPath += "[" + std::string(attr->Name()) + "=" + attr->Value() + "]";
        attr = attr->Next();
    }

    // If this element has text content (and no child elements), store it
    if (element->GetText() && !element->FirstChildElement()) {
        params[currentPath] = element->GetText();
    }

    // Recurse into child elements
    for (XMLElement* child = element->FirstChildElement(); child; child = child->NextSiblingElement()) {
        parseXMLNode(child, currentPath, params);
    }
}

int parseXMLFile(const std::string& filePath, std::unordered_map<std::string, std::string>& params) {
    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(filePath.c_str()) != XML_SUCCESS) {
        std::cerr << "Failed to load XML file: " << filePath << "\n";
        return 1;
    }

    XMLElement* root = doc.RootElement();
    if (root) {
        parseXMLNode(root, "", params);
    }
    return 0;
}
