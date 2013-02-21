var newCollectionView = Backbone.View.extend({
  el: '#modalPlaceholder',
  initialize: function () {
  },

  template: new EJS({url: '/_admin/html/js/templates/newCollectionView.ejs'}),

  render: function() {
    var self = this;
    $(this.el).html(this.template.text);
    $('#add-collection').modal('show');
    $('#add-collection').on('hidden', function () {
      self.hidden();
    });

    return this;
  },

  events: {
    "click #save-new-collection" : "saveNewCollection"
  },

  hidden: function () {
    window.location.hash = "#";
  },

  clearModal: function() {
  },

  saveNewCollection: function() {
    var self = this;

    var collName = $('#new-collection-name').val();
    var collSize = $('#new-collection-size').val();
    var collType = $('#new-collection-type').val();
    var collSync = $('#new-collection-sync').val();
    var isSystem = (collName.substr(0, 1) === '_');
    var wfs = (collSync == "true");

    if (collSize == '') {
      journalSizeString = '';
    }
    else {
      collSize = JSON.parse(collSize) * 1024 * 1024;
      journalSizeString = ', "journalSize":' + collSize;
    }
    if (collName == '') {
      alert("No collection name entered. Aborting...");
      return 0;
    }

    var returnval = window.arangoCollectionsStore.newCollection(collName, wfs, isSystem, journalSizeString, collType);
    if (returnval === true) {
      self.hidden();
      $("#add-collection").modal('hide');
      alert("Collection created");
    }
    else {
      self.hidden();
      $("#add-collection").modal('hide');
      alert("Collection error");
    }

  }

});
