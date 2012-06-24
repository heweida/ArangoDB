////////////////////////////////////////////////////////////////////////////////
/// @brief http response
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2012 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is triAGENS GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
/// @author Achim Brandt
/// @author Copyright 2008-2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "HttpResponse.h"

#include "Basics/StringUtils.h"

#ifdef TRI_ENABLE_ZEROMQ
#include "ProtocolBuffers/arangodb.pb.h"
#endif

using namespace triagens::basics;
using namespace triagens::rest;
using namespace std;

// -----------------------------------------------------------------------------
// --SECTION--                                             static public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Rest
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief http response string
////////////////////////////////////////////////////////////////////////////////

string HttpResponse::responseString (HttpResponseCode code) {
  switch (code) {

    //  Success 2xx
    case OK:                   return "200 OK";
    case CREATED:              return "201 Created";
    case ACCEPTED:             return "202 Accepted";
    case PARTIAL:              return "203 Partial Information";
    case NO_CONTENT:           return "204 No Content";

    //  Redirection 3xx
    case MOVED_PERMANENTLY:    return "301 Moved";
    case FOUND:                return "302 Found";
    case SEE_OTHER:            return "303 See Other";
    case NOT_MODIFIED:         return "304 Not Modified";
    case TEMPORARY_REDIRECT:   return "307 Temporary Redirect";

    //  Error 4xx, 5xx
    case BAD:                  return "400 Bad Request";
    case UNAUTHORIZED:         return "401 Unauthorized";
    case PAYMENT:              return "402 Payment Required";
    case FORBIDDEN:            return "403 Forbidden";
    case NOT_FOUND:            return "404 Not Found";
    case METHOD_NOT_ALLOWED:   return "405 Method";
    case CONFLICT:             return "409 Conflict";
    case PRECONDITION_FAILED:  return "412 Precondition Failed";
    case UNPROCESSABLE_ENTITY: return "422 Unprocessable Entity";

    case SERVER_ERROR:         return "500 Internal Error";
    case NOT_IMPLEMENTED:      return "501 Not implemented";
    case BAD_GATEWAY:          return "502 Bad Gateway";
    case SERVICE_UNAVAILABLE:  return "503 Service Temporarily Unavailable";

    // default
    default:                   return StringUtils::itoa((int) code) + " (unknown HttpResponseCode)";
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get http response code from string
////////////////////////////////////////////////////////////////////////////////

HttpResponse::HttpResponseCode HttpResponse::responseCode (const string& str) {
  int number = ::atoi(str.c_str());

  switch (number) {
    case 200: return OK;
    case 201: return CREATED;
    case 202: return ACCEPTED;
    case 203: return PARTIAL;
    case 204: return NO_CONTENT;

    case 301: return MOVED_PERMANENTLY;
    case 302: return FOUND;
    case 303: return SEE_OTHER;
    case 304: return NOT_MODIFIED;
    case 307: return TEMPORARY_REDIRECT;

    case 400: return BAD;
    case 401: return UNAUTHORIZED;
    case 402: return PAYMENT;
    case 403: return FORBIDDEN;
    case 404: return NOT_FOUND;
    case 405: return METHOD_NOT_ALLOWED;
    case 409: return CONFLICT;
    case 412: return PRECONDITION_FAILED;
    case 422: return UNPROCESSABLE_ENTITY;

    case 500: return SERVER_ERROR;
    case 501: return NOT_IMPLEMENTED;
    case 502: return BAD_GATEWAY;
    case 503: return SERVICE_UNAVAILABLE;

    default:  return NOT_IMPLEMENTED;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Rest
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief constructs a new http response
////////////////////////////////////////////////////////////////////////////////

HttpResponse::HttpResponse ()
  : _code(NOT_IMPLEMENTED),
    _headers(5),
    _body(TRI_UNKNOWN_MEM_ZONE),
    _isHeadResponse(false),
    _bodySize(0),
    _freeables() {
}

////////////////////////////////////////////////////////////////////////////////
/// @brief constructs a new http response
////////////////////////////////////////////////////////////////////////////////

HttpResponse::HttpResponse (string const& header)
  : _code(NOT_IMPLEMENTED),
    _headers(5),
    _body(TRI_UNKNOWN_MEM_ZONE),
    _isHeadResponse(false),
    _bodySize(0),
    _freeables() {
  setHeaders(header, true);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief constructs a new http response
////////////////////////////////////////////////////////////////////////////////

HttpResponse::HttpResponse (HttpResponseCode code)
  : _code(code),
    _headers(5),
    _body(TRI_UNKNOWN_MEM_ZONE),
    _isHeadResponse(false),
    _bodySize(0),
    _freeables() {
  char* headerBuffer = StringUtils::duplicate("server\ntriagens GmbH High-Performance HTTP Server\n"
                                              "connection\nKeep-Alive\n"
                                              "content-type\ntext/plain;charset=utf-8\n");
  _freeables.push_back(headerBuffer);
  
  bool key = true;
  char* startKey = headerBuffer;
  char* startValue = 0;
  char* end = headerBuffer + strlen(headerBuffer);

  for (char* ptr = headerBuffer;  ptr < end;  ++ptr) {
    if (*ptr == '\n') {
      *ptr = '\0';
      
      if (key) {
        startValue = ptr + 1;
        key = false;
      }
      else {
        _headers.insert(startKey, startValue);
        
        startKey = ptr + 1;
        startValue = 0;
        key = true;
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief deletes a http response
////////////////////////////////////////////////////////////////////////////////

HttpResponse::~HttpResponse () {
  for (vector<char const*>::iterator i = _freeables.begin();  i != _freeables.end();  ++i) {
    delete[] (*i);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Rest
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the response code
////////////////////////////////////////////////////////////////////////////////

HttpResponse::HttpResponseCode HttpResponse::responseCode () {
  return _code;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the content length
////////////////////////////////////////////////////////////////////////////////

size_t HttpResponse::contentLength () {
  if (_isHeadResponse) {
    return _bodySize;
  }
  else {
    Dictionary<char const*>::KeyValue const* kv = _headers.lookup("content-length");
    
    if (kv == 0) {
      return 0;
    }
    
    return StringUtils::uint32(kv->_value);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief sets the content type
////////////////////////////////////////////////////////////////////////////////

void HttpResponse::setContentType (string const& contentType) {
  setHeader("content-type", contentType);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns a header field
////////////////////////////////////////////////////////////////////////////////

string HttpResponse::header (string const& key) const {
  string k = StringUtils::tolower(key);
  Dictionary<char const*>::KeyValue const* kv = _headers.lookup(k.c_str());
  
  if (kv == 0) {
    return "";
  }
  else {
    return kv->_value;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns a header field
////////////////////////////////////////////////////////////////////////////////

string HttpResponse::header (string const& key, bool& found) const {
  string k = StringUtils::tolower(key);
  Dictionary<char const*>::KeyValue const* kv = _headers.lookup(k.c_str());
  
  if (kv == 0) {
    found = false;
    return "";
  }
  else {
    found = true;
    return kv->_value;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns all header fields
////////////////////////////////////////////////////////////////////////////////

map<string, string> HttpResponse::headers () const {
  basics::Dictionary<char const*>::KeyValue const* begin;
  basics::Dictionary<char const*>::KeyValue const* end;
  
  map<string, string> result;
  
  for (_headers.range(begin, end);  begin < end;  ++begin) {
    char const* key = begin->_key;
    
    if (key == 0) {
      continue;
    }
    
    result[key] = begin->_value;
  }
  
  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief sets a header field
////////////////////////////////////////////////////////////////////////////////

void HttpResponse::setHeader (string const& key, string const& value) {
  string lk = StringUtils::tolower(key);
  
  if (value.empty()) {
    _headers.erase(lk.c_str());
  }
  else {
    StringUtils::trimInPlace(lk);
    
    char const* k = StringUtils::duplicate(lk);
    char const* v = StringUtils::duplicate(value);
    
    _headers.insert(k, lk.size(), v);
    
    _freeables.push_back(k);
    _freeables.push_back(v);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief sets many header fields
////////////////////////////////////////////////////////////////////////////////

void HttpResponse::setHeaders (string const& headers, bool includeLine0) {
  
  // make a copy we can change, this buffer will be deleted in the destructor
  char* headerBuffer = new char[headers.size() + 1];
  memcpy(headerBuffer, headers.c_str(), headers.size() + 1);
  
  _freeables.push_back(headerBuffer);
  
  // check for '\n' (we check for '\r' later)
  int lineNum = includeLine0 ? 0 : 1;
  
  char* start = headerBuffer;
  char* end = headerBuffer + headers.size();
  char* end1 = start;
  
  for (; end1 < end && *end1 != '\n';  ++end1) {
  }
  
  while (start < end) {
    char* end2 = end1;
    
    if (start < end2 && end2[-1] == '\r') {
      --end2;
    }
    
    // the current line is [start, end2)
    if (start < end2) {
      
      // now split line at the first spaces
      char* end3 = start;
      
      for (; end3 < end2 && *end3 != ' ' && *end3 != ':'; ++end3) {
        *end3 = ::tolower(*end3);
      }
      
      // the current token is [start, end3) and all in lower case
      if (lineNum == 0) {
        
        // the start should be HTTP/1.1 followed by blanks followed by the result code
        if (start + 8 <= end3 && memcmp(start, "http/1.1", 8) == 0) {
          
          char* start2 = end3;
          
          for (; start2 < end2 && (*start2 == ' ' || *start2 == ':');  ++start2) {
          }
          
          if (start2 < end2) {
            *end2 = '\0';
            
            _code = static_cast<HttpResponseCode>(::atoi(start2));
          }
          else {
            _code = NOT_IMPLEMENTED;
          }
        }
        else {
          _code = NOT_IMPLEMENTED;
        }
      }
      
      // normal header field, key is [start, end3) and the value is [start2, end4)
      else {
        for (;  end3 < end2 && *end3 != ':';  ++end3) {
          *end3 = ::tolower(*end3);
        }
        
        *end3 = '\0';
        
        if (end3 < end2) {
          char* start2 = end3 + 1;
          
          for (;  start2 < end2 && *start2 == ' ';  ++start2) {
          }
          
          char* end4 = end2;
          
          for (;  start2 < end4 && *(end4 - 1) == ' ';  --end4) {
          }
          
          *end4 = '\0';
          
          _headers.insert(start, end3 - start, start2);
        }
        else {
          _headers.insert(start, end3 - start, end3);
        }
          }
    }
    
    start = end1 + 1;
    
    for (end1 = start; end1 < end && *end1 != '\n';  ++end1) {
    }
    
    lineNum++;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief swaps data
////////////////////////////////////////////////////////////////////////////////

HttpResponse* HttpResponse::swap () {
  HttpResponse* response = new HttpResponse(_code);
  
  response->_headers.swap(&_headers);
  response->_body.swap(&_body);
  response->_freeables.swap(_freeables);
  
  bool isHeadResponse = response->_isHeadResponse;
  response->_isHeadResponse = _isHeadResponse;
  _isHeadResponse = isHeadResponse;
  
  size_t bodySize = response->_bodySize;
  response->_bodySize = _bodySize;
  _bodySize = bodySize;
  
  return response;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief writes the header
////////////////////////////////////////////////////////////////////////////////

void HttpResponse::writeHeader (StringBuffer* output) {
  output->appendText("HTTP/1.1 ");
  output->appendText(responseString(_code));
  output->appendText("\r\n");
  
  basics::Dictionary<char const*>::KeyValue const* begin;
  basics::Dictionary<char const*>::KeyValue const* end;
  
  bool seenTransferEncoding = false;
  string transferEncoding;
  
  for (_headers.range(begin, end);  begin < end;  ++begin) {
    char const* key = begin->_key;
    
    if (key == 0) {
      continue;
    }
    
    // ignore content-length
    if (strcmp(key, "content-length") == 0) {
      continue;
    }
    
    if (strcmp(key, "transfer-encoding") == 0) {
      seenTransferEncoding = true;
      transferEncoding = begin->_value;
      continue;
    }
    
    char const* value = begin->_value;
    
    output->appendText(key);
    output->appendText(": ");
    output->appendText(value);
    output->appendText("\r\n");
  }
  
  if (seenTransferEncoding && transferEncoding == "chunked") {
    output->appendText("transfer-encoding: chunked\r\n");
  }
  else {
    if (seenTransferEncoding) {
      output->appendText("transfer-encoding: ");
      output->appendText(transferEncoding);
      output->appendText("\r\n");
    }
    
    output->appendText("content-length: ");
    
    if (_isHeadResponse) {
      output->appendInteger(_bodySize);
    }
    else {
      output->appendInteger(_body.length());
    }
    
    output->appendText("\r\n");
  }
  
  output->appendText("\r\n");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief writes the message to a protocol buffer
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_ZEROMQ

void HttpResponse::write (PB_ArangoBatchMessage* message) {
  message->set_type(PB_BLOB_RESPONSE);
  PB_ArangoBlobResponse* blob = message->mutable_blobresponse();

  blob->set_status((int32_t) _code);

  // copy the headers
  basics::Dictionary<char const*>::KeyValue const* begin;
  basics::Dictionary<char const*>::KeyValue const* end;
  
  string contentType = "text/plain";

  for (_headers.range(begin, end);  begin < end;  ++begin) {
    char const* key = begin->_key;
    
    if (key == 0) {
      continue;
    }
    
    // ignore content-length
    if (strcmp(key, "content-length") == 0) {
      continue;
    }
    
    if (strcmp(key, "transfer-encoding") == 0) {
      continue;
    }

    if (strcmp(key, "connection") == 0) {
      continue;
    }

    if (strcmp(key, "server") == 0) {
      continue;
    }

    char const* value = begin->_value;
    
    if (strcmp(key, "content-type") == 0) {
      contentType = value;
      continue;
    }
    
    PB_ArangoKeyValue* kv = blob->add_headers();

    kv->set_key(key);
    kv->set_value(value);
  }

  // set the content type
  if (StringUtils::isPrefix(contentType, "application/json")) {
    blob->set_contenttype(PB_JSON_CONTENT);
  }
  else {
    blob->set_contenttype(PB_TEXT_CONTENT);
  }

  // check the body
  if (_isHeadResponse) {
    blob->set_contentlength(_bodySize);
  }
  else {
    blob->set_contentlength(_body.length());

    string content(_body.c_str(), _body.length());
    blob->set_content(content);
  }
}

#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the size of the body
////////////////////////////////////////////////////////////////////////////////

size_t HttpResponse::bodySize () {
  if (_isHeadResponse) {
    return _bodySize;
  }
  else {
    return _body.length();
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the body
////////////////////////////////////////////////////////////////////////////////

StringBuffer& HttpResponse::body () {
  return _body;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief indicates a head response
////////////////////////////////////////////////////////////////////////////////

void HttpResponse::headResponse (size_t size) {
  _body.clear();
  _isHeadResponse = true;
  _bodySize = size;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}\\)"
// End: