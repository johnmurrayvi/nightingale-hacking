/**
//
// BEGIN SONGBIRD GPL
// 
// This file is part of the Songbird web player.
//
// Copyright(c) 2005-2008 POTI, Inc.
// http://songbirdnest.com
// 
// This file may be licensed under the terms of of the
// GNU General Public License Version 2 (the "GPL").
// 
// Software distributed under the License is distributed 
// on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either 
// express or implied. See the GPL for the specific language 
// governing rights and limitations.
//
// You should have received a copy of the GPL along with this 
// program. If not, go to http://www.gnu.org/licenses/gpl.html
// or write to the Free Software Foundation, Inc., 
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
// 
// END SONGBIRD GPL
//
 */

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

const SONGBIRD_PARSERERRORHANDLER_CONTRACTID = "@songbirdnest.com/Songbird/ParserErrorHandler;1";
const SONGBIRD_PARSERERRORHANDLER_CLASSDESC = "Songbird Parser Error Handler";
const SONGBIRD_PARSERERRORHANDLER_CLASSNAME = "ParserErrorHandler";
const SONGBIRD_PARSERERRORHANDLER_CID = Components.ID("{213a0ebb-12b3-492f-bc4c-f472f8f24d2c}");

const MSG_ERROR_UNDEFINEDENTITY = '[JavaScript Error: "undefined entity"';
const MSG_ERROR_ENTITYPROCESSING = '[JavaScript Error: "error in processing external entity reference"';

var gBusy = false;

var consoleListener = {
  observe: function(msg) 
  {
    if (msg.message.substr(0, MSG_ERROR_UNDEFINEDENTITY.length) == MSG_ERROR_UNDEFINEDENTITY ||
        msg.message.substr(0, MSG_ERROR_ENTITYPROCESSING.length) == MSG_ERROR_ENTITYPROCESSING) {
      dump("parseError:" + msg.message + "\n");

      var prefs = Components.classes["@mozilla.org/preferences-service;1"].
                              getService(Components.interfaces.nsIPrefBranch);
      var curLocale = "en-US";
      try {
        curLocale = prefs.getCharPref("general.useragent.locale");
      }
      catch (e) { }

      if (!gBusy && curLocale != "en-US") { 
      
        try {                        
          gBusy = true;
          
          var wWatcher = Components.classes["@mozilla.org/embedcomp/window-watcher;1"]
                                   .getService(Components.interfaces.nsIWindowWatcher);
          var wMediator = Components.classes["@mozilla.org/appshell/window-mediator;1"]
                                    .getService(Components.interfaces.nsIWindowMediator);
          var mainWindow = wMediator.getMostRecentWindow("Songbird:Main");

          wWatcher.openWindow(mainWindow,
                    "chrome://songbird/content/xul/parserError.xul",
                    "_blank",
                    "chrome,modal=yes,centerscreen,resizable=no",
                    msg);
        } catch (e) {
        }
        gBusy = false;
      }
    }
  },
  
  QueryInterface : function(aIID)
  {
    if (!aIID.equals(Components.interfaces.nsIConsoleListener) &&
        !aIID.equals(Components.interfaces.nsISupports)) 
    {
      throw Components.results.NS_ERROR_NO_INTERFACE;
    }
    return this;
  }
};

var gOS = null;
var gConsole = null;

function ParserErrorHandler() {
  try {
    gConsole = Components.classes["@mozilla.org/consoleservice;1"]
                        .getService(Components.interfaces.nsIConsoleService);
    gOS      = Components.classes["@mozilla.org/observer-service;1"]
                        .getService(Components.interfaces.nsIObserverService);
    
    if (gOS.addObserver) {
      // We need to unhook things on shutdown
      gOS.addObserver(this, "xpcom-shutdown", false);
    }
  } catch (e) { }
}

ParserErrorHandler.prototype.constructor = ParserErrorHandler;

ParserErrorHandler.prototype = {
  classDescription: SONGBIRD_PARSERERRORHANDLER_CLASSDESC,
  className: SONGBIRD_PARSERERRORHANDLER_CLASSNAME,
  classID: SONGBIRD_PARSERERRORHANDLER_CID,
  contractID: SONGBIRD_PARSERERRORHANDLER_CONTRACTID,
  _xpcom_categories: [{
    category: "profile-after-change",
    service: true
  }],

  _init: function() {
    try {
      gConsole.registerListener(consoleListener);
    } catch (e) { }
  },
  
  _deinit: function() {
    try {
      gConsole.unregisterListener(consoleListener);
    } catch (e) { }
  },

  // watch for XRE startup and shutdown messages 
  observe: function(subject, topic, data) {
    switch (topic) {
    case "profile-after-change":
      // Preferences are initialized, ready to start the service
      this._init();
      break;
    case "xpcom-shutdown":
      gOS.removeObserver(this, "xpcom-shutdown");
      this._deinit();
      
      // Release Services to avoid memory leaks
      gOS       = null;
      gConsole  = null;
      break;
    }
  },

  /**
   * See nsISupports.idl
   */
  QueryInterface: XPCOMUtils.generateQI([
    Components.interfaces.nsIObserver,
    Components.interfaces.nsISupports
  ])
}; // ParserErrorHandler.prototype

/**
 * ----------------------------------------------------------------------------
 * Registration for XPCOM
 * ----------------------------------------------------------------------------
 */

var NSGetFactory = XPCOMUtils.generateNSGetFactory([ParserErrorHandler]);
