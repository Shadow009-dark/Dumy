// html_cleaner.h - strips HTML down to readable plain text for RAG context.
#pragma once

#include <string>

namespace dumy {

// Removes scripts/styles/menus/comments/tags, decodes entities and returns
// readable text. Block elements (<p>, <br>, <li>, headings...) become
// newlines so paragraphs stay separated.
std::string html_to_text(const std::string& html);

// Extracts the <title> content (trimmed, entities decoded); "" if absent.
std::string html_title(const std::string& html);

// Decodes &amp; &lt; &gt; &quot; &#39; &nbsp; &#NN; &#xNN; entities.
std::string decode_entities(const std::string& s);

}  // namespace dumy
