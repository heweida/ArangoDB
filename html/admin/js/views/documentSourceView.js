var documentSourceView = Backbone.View.extend({
  el: '#content',
  init: function () {
  },

  template: new EJS({url: '/_admin/html/js/templates/documentSourceView.ejs'}),

  render: function() {
    $(this.el).html(this.template.text);
    return this;
  }

});
