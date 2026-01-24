#include "Persistence.h"

#include "Bytes.h"

using namespace RNS;

// Single definition of persistence globals - declared extern in Persistence.h
// Note: JsonDocument (v7 API) uses elastic allocation, no size parameter needed
namespace RNS { namespace Persistence {
    JsonDocument _document;
    Bytes _buffer(Type::Persistence::BUFFER_MAXSIZE);
}}
