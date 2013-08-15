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
 

/**
 * \file sbAddonMetadata.js
 * \brief Provides an nsIRDFDataSource with the contents of all 
 *        addon install.rdf files.
 */ 

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");
Components.utils.import("resource://gre/modules/AddonManager.jsm");
 
const CONTRACTID = "@mozilla.org/rdf/datasource;1?name=addon-metadata";
const CLASSNAME = "AddonMetadata";
const CLASSDESC = "Songbird Addon Metadata Datasource";
const CID = Components.ID("{a1edd551-0f29-4ce9-aebc-92fbee77f37e}");
const IID = Components.interfaces.nsIRDFDataSource;

const FILE_INSTALL_MANIFEST = "install.rdf";
const FILE_EXTENSIONS       = "extensions.sqlite";
const FILE_ADDONMETADATA    = "addon-metadata.rdf";

const PREF_LASTMODIFIED = "songbird.addonmetadata.lastModifiedTime";

const KEY_PROFILEDIR                  = "ProfD";

const RDFURI_INSTALL_MANIFEST_ROOT    = "urn:mozilla:install-manifest";
const RDFURI_ADDON_ROOT               = "urn:songbird:addon:root"
const PREFIX_ADDON_URI                = "urn:songbird:addon:";
const PREFIX_NS_EM                    = "http://www.mozilla.org/2004/em-rdf#";
const PREFIX_NS_SONGBIRD              = "http://www.songbirdnest.com/2007/addon-metadata-rdf#";
const PREFIX_ITEM_URI                 = "urn:mozilla:item:";

function SONGBIRD_NS(property) {
  return PREFIX_NS_SONGBIRD + property;
}

function EM_NS(property) {
  return PREFIX_NS_EM + property;
}

function ADDON_NS(id) {
  return PREFIX_ADDON_URI + id;
}

function ITEM_NS(id) {
  return PREFIX_ITEM_URI + id;
}


/**
 * /class AddonMetadata
 * /brief Provides an nsIRDFDataSource with the contents of all 
 *        addon install.rdf files.
 */
function AddonMetadata() {
  //debug("\nAddonMetadata: constructed\n");
  this._RDF = Components.classes["@mozilla.org/rdf/rdf-service;1"]
              .getService(Components.interfaces.nsIRDFService);

  try {
    // If possible, load the cached datasource from disk. 
    // Otherwise rebuild it by reading the metadata from all addons
    if (!this._loadDatasource() || this._isRebuildRequired()) {
      //debug("AddonMetadata: rebuilding addon metadata datasource\n");
      this._purgeDatasource();

      // XXX XUL9 - BAD! This is terrible! But it's the only
      // thing that works right now :(
      var done = false;
      var thread = Components.classes["@mozilla.org/thread-manager;1"]
                             .getService(Components.interfaces.nsIThreadManager)
                             .currentThread;

      this._buildDatasource(function() {
        // dump("AddonMetadata: in callback after _buildDatasource\n");
        done = true;
      });
      while (!done) {
        // dump("AddonMetadata: processNextEvent\n");
        thread.processNextEvent(true);
      }
    }
  } catch (e) {
    dump("AddonMetadata: Constructor Error: " + e.toString());
  }

  var os  = Components.classes["@mozilla.org/observer-service;1"]
            .getService(Components.interfaces.nsIObserverService);
  os.addObserver(this, "xpcom-shutdown", false);
};

AddonMetadata.prototype = {
  classDescription: CLASSDESC,
  className: CLASSNAME,
  classID: CID,
  contractID: CONTRACTID,

  constructor: AddonMetadata,

  _RDF: null,
  _datasource: null,


  /**
   * Return true if cached addons metadata datasource is no longer valid.
   * Uses the last modified date on the extension manager's data file to
   * determine if extensions have changed.
   */
  _isRebuildRequired: function _isRebuildRequired() {
    var emDataFile = this._getProfileFile(FILE_EXTENSIONS);
    
    if (!emDataFile.exists()) {      
      //debug("AddonMetadata._isRebuildRequired: " + FILE_EXTENSIONS + " not found\n");
      return true;
    }

    var newLastModified = emDataFile.lastModifiedTime.toString();

    // When was the last time we noticed that extensions.rdf had been modified
    var prefs = Components.classes["@mozilla.org/preferences-service;1"]
                          .getService(Components.interfaces.nsIPrefBranch);
    var lastModified = 0;
    try {  
      lastModified = prefs.getCharPref(PREF_LASTMODIFIED);
    } catch (e) {}    
    
    // If the extensions.sqlite hasn't changed, then we don't need to rebuild
    //debug("AddonMetadata._isRebuildRequired: " + lastModified + " == " + newLastModified + "\n");
    if (lastModified == newLastModified) {
      return false;
    }
        
    // Store the new last modified time
    prefs.setCharPref(PREF_LASTMODIFIED, newLastModified);

    //debug("AddonMetadata._isRebuildRequired: true\n");

    // extensions.sqlite has changed, so we will need to rebuild the addons datasource.
    return true;
  },


  /**
   * Attempt to load the data source from disk.  Return false 
   * if the datasource needs populating. 
   */
  _loadDatasource: function _loadDatasource() {
    //debug("\nAddonMetadata: _loadDatasource \n");

    var file = this._getProfileFile(FILE_ADDONMETADATA);
    
    try { 
      this._datasource = this._getDatasource(file);
    } catch (e) {
      // Load did not go ok.  Will need to rebuild
      return false;
    }
    
    if (!file.exists()) {
      // dump("AddonMetadata::_loadDatasource -- file doesn't exist\n");
      return false;
    }
    
    return true;
  },
  

  /**
   * Clean out the contents of the datasource
   */
  _purgeDatasource: function _purgeDatasource() {
    //debug("\nAddonMetadata: _purgeDatasource \n");

    var file = this._getProfileFile(FILE_ADDONMETADATA);
    
    // Make sure the RDF service isn't caching our ds
    this._RDF.UnregisterDataSource(this._datasource);
    
    // Kill the file
    try { 
      if (file.exists()) {
        file.remove(false);
      }
    } catch (e) {
      debug("\nAddonMetadata: _purgeDatasource: Could not remove " 
            + "addon-metadata.rdf. Bad things may happen.\n");
    }

    // Reload the datasource.  Since the file no longer exists
    // this will result in a new empty DS.
    this._loadDatasource();
  },


  /**
   * Given a filename, returns an nsIFile pointing to 
   * profile/filename
   */
  _getProfileFile: function _getProfileFile(filename) {
    // get profile directory
    var file = Components.classes["@mozilla.org/file/directory_service;1"]
                         .getService(Components.interfaces.nsIProperties)
                         .get("ProfD", Components.interfaces.nsIFile);
    var localFile = file.QueryInterface(Components.interfaces.nsILocalFile);
    localFile.appendRelativePath(filename)
    return file;
  },


  
  /**
   * Populate the datasource with the contents of all 
   * addon install.rdf files.
   */
  _buildDatasource: function _buildDatasource(callback) {
    //debug("\nAddonMetadata: _buildDatasource \n");

    // Make a container to list all installed extensions
    var itemRoot = this._RDF.GetResource(RDFURI_ADDON_ROOT);    
    var cu = Components.classes["@mozilla.org/rdf/container-utils;1"]
                       .getService(Components.interfaces.nsIRDFContainerUtils);
    var container = cu.MakeSeq(this._datasource, itemRoot);

    var that = this;
    AddonManager.getAllAddons(function(aAddons) {
      for (var i = 0; i < aAddons.length; i++) {
        var addon = aAddons[i];
        if (addon.type == "extension") {
          // If the extension is disabled, do not include it in our datasource 
          if (addon.userDisabled || addon.appDisabled) {
            //debug("\nAddonMetadata:  id {" + id +  "} is disabled.\n");
            continue;
          }

          //debug("\nAddonMetadata:  loading install.rdf for id {" + id +  "}\n");

          var installManifestFile = addon.getResourceURI(FILE_INSTALL_MANIFEST).QueryInterface(Components.interfaces.nsIFileURL).file;

          if (!installManifestFile.exists()) {
            that._reportErrors(["install.rdf for id " + addon.id +  " was not found " + 
                                "at location " + installManifestFile.path]);
          }
          
          var manifestDS = that._getDatasource(installManifestFile);
          var itemNode = that._RDF.GetResource(ADDON_NS(addon.id));
          // Copy the install.rdf metadata into the master datasource
          that._copyManifest(itemNode, manifestDS);
          
          // Add the new install.rdf root to the list of extensions
          container.AppendElement(itemNode);
        }
      }

      // Save changes
      that._datasource.QueryInterface(Components.interfaces.nsIRDFRemoteDataSource)
                      .Flush();
      //debug("\nAddonMetadata: _buildDatasource complete \n");

      callback();
    });
  },


  /**
   * Copy all nodes and assertions into the main datasource,
   * renaming the install manifest to the id of the extension.
   */
  _copyManifest:  function _copyManifest(itemNode, manifestDS) {  
    // Copy all resources from the manifest to the main DS
    var resources = manifestDS.GetAllResources();
    while (resources.hasMoreElements()) {
      var resource = resources.getNext().QueryInterface(Components.interfaces.nsIRDFResource);
      // dump("    resource.ValueUTF8 = "+resource.ValueUTF8+"\n");

      // Get all arcs out of the resource
      var arcs = manifestDS.ArcLabelsOut(resource);
      while (arcs.hasMoreElements()) {
        var arc = arcs.getNext().QueryInterface(Components.interfaces.nsIRDFResource);
        // dump("        arc.ValueUTF8 = "+arc.ValueUTF8+"\n");

        // For each arc, get all targets
        var targets = manifestDS.GetTargets(resource, arc, true);
        while (targets.hasMoreElements()) {
          var target = targets.getNext().QueryInterface(Components.interfaces.nsIRDFNode);

          // If this resource is the manifest root, replace it
          // with a resource representing the current addon
          newResource = resource;
          if (resource.ValueUTF8 == RDFURI_INSTALL_MANIFEST_ROOT) {
            newResource = itemNode;
          }

          // dump("    Asserting nR = "+newResource.ValueUTF8+", arc = "+arc.ValueUTF8+"\n");
          // Otherwise, assert into the main ds
          this._datasource.Assert(newResource, arc, target, true);
        }
      }                   
    }
  },


  /**
   * Gets a datasource from a file.
   * @param   file
   *          The nsIFile that containing RDF/XML
   * @returns RDF datasource
   */
  _getDatasource: function _getDatasource(file) {
    var ioServ = Components.classes["@mozilla.org/network/io-service;1"]
                           .getService(Components.interfaces.nsIIOService);
    var fph = ioServ.getProtocolHandler("file")
                    .QueryInterface(Components.interfaces.nsIFileProtocolHandler);

    var fileURL = fph.getURLSpecFromFile(file);
    var ds = this._RDF.GetDataSourceBlocking(fileURL);
    return ds;
  },


  
  _deinit: function _deinit() {
    //debug("\nAddonMetadata: deinit\n");

    this._RDF = null;
    this._datasource = null;
  },
    
  

  // watch for XRE startup and shutdown messages 
  observe: function(subject, topic, data) {
    var os = Components.classes["@mozilla.org/observer-service;1"]
                       .getService(Components.interfaces.nsIObserverService);
    switch (topic) {
    case "xpcom-shutdown":
      os.removeObserver(this, "xpcom-shutdown");
      this._deinit();
      break;
    }
  },
  
  
  
  _reportErrors: function _reportErrors(errorList) {
    var consoleService = Components.classes["@mozilla.org/consoleservice;1"].
         getService(Components.interfaces.nsIConsoleService);
    for (var i = 0; i  < errorList.length; i++) {
      consoleService.logStringMessage("Addon Metadata: " + errorList[i]);
      debug("AddonMetadata: " + errorList[i] + "\n");
    }
  },



  /**
   * See nsISupports.idl
   */
  QueryInterface: function(iid) {
    if (!iid.equals(IID) &&
        !iid.equals(Components.interfaces.nsIObserver) && 
        !iid.equals(Components.interfaces.nsISupports))
      throw Components.results.NS_ERROR_NO_INTERFACE;
    
    // THIS IS SO WRONG! 
    // We can get away with it though since we really only want the datasource
    // and never the object that builds it.
    if (iid.equals(IID)) {
      return this._datasource;
    }
    
    return this;
  }
  // QueryInterface: XPCOMUtils.generateQI([
  //   IID,
  //   Components.interfaces.nsIObserver,
  //   Components.interfaces.nsISupports
  // ])
}; // AddonMetadata.prototype


/**
 * ----------------------------------------------------------------------------
 * Registration for XPCOM
 * ----------------------------------------------------------------------------
 */
var NSGetFactory = XPCOMUtils.generateNSGetFactory([AddonMetadata]);
