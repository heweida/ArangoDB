////////////////////////////////////////////////////////////////////////////////
/// @brief http server
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
/// @author Copyright 2009-2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_HTTP_SERVER_HTTP_SERVER_H
#define TRIAGENS_HTTP_SERVER_HTTP_SERVER_H 1

#include "HttpServer/HttpServer.h"

#include "GeneralServer/GeneralServerDispatcher.h"
#include "HttpServer/HttpCommTask.h"

// -----------------------------------------------------------------------------
// --SECTION--                                              forward declarations
// -----------------------------------------------------------------------------

namespace triagens {
  namespace rest {
    class HttpHandlerFactory;
    class HttpListenTask;

// -----------------------------------------------------------------------------
// --SECTION--                                                  class HttpServer
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup HttpServer
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief http server implementation
////////////////////////////////////////////////////////////////////////////////

    class HttpServer : public GeneralServerDispatcher<HttpServer, HttpHandlerFactory, HttpCommTask> {

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup HttpServer
/// @{
////////////////////////////////////////////////////////////////////////////////

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief constructs a new http server
////////////////////////////////////////////////////////////////////////////////

        HttpServer (Scheduler* scheduler, Dispatcher* dispatcher);

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup HttpServer
/// @{
////////////////////////////////////////////////////////////////////////////////

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief activates the maintenance mode
////////////////////////////////////////////////////////////////////////////////

        void activateMaintenance ();

////////////////////////////////////////////////////////////////////////////////
/// @brief activates the maintenance mode
////////////////////////////////////////////////////////////////////////////////

        void activateMaintenance (MaintenanceCallback* callback);

////////////////////////////////////////////////////////////////////////////////
/// @brief deactivates the maintenance mode
////////////////////////////////////////////////////////////////////////////////

        void deactivateMaintenance ();

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the number of active handlers
////////////////////////////////////////////////////////////////////////////////

        size_t numberActiveHandlers ();

////////////////////////////////////////////////////////////////////////////////
/// @brief checks if to close connection if keep-alive is missing
////////////////////////////////////////////////////////////////////////////////

        bool getCloseWithoutKeepAlive () const;

////////////////////////////////////////////////////////////////////////////////
/// @brief close connection if keep-alive is missing
////////////////////////////////////////////////////////////////////////////////

        void setCloseWithoutKeepAlive (bool value = true);

////////////////////////////////////////////////////////////////////////////////
/// @brief sets a new handler factory
////////////////////////////////////////////////////////////////////////////////

        void setHandlerFactory (HttpHandlerFactory* factory);

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a maintenance request
////////////////////////////////////////////////////////////////////////////////

        HttpRequest* createMaintenanceRequest (char const* ptr, size_t length) const;

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a maintenance handler
////////////////////////////////////////////////////////////////////////////////

        HttpHandler* createMaintenanceHandler () const;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup HttpServer
/// @{
////////////////////////////////////////////////////////////////////////////////

      private:

////////////////////////////////////////////////////////////////////////////////
/// @brief close connection without explicit keep-alive
////////////////////////////////////////////////////////////////////////////////

        bool _closeWithoutKeepAlive;
    };
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

#endif

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End: