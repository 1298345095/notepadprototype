require 'fox16'
require 'fiddle/import'

module ForkProbe
  extend Fiddle::Importer
  dlload File.expand_path(ARGV.fetch(0))
  extern 'int NP4_Register()'
  extern 'void NP4_Release()'
  extern 'int NP4_AttachAndVerify(void*)'
end
app = Fox::FXApp.new('NotepadPrototype', 'ToolkitStudy')
window = Fox::FXMainWindow.new(app, 'FXRuby native host', nil, nil, Fox::DECOR_ALL, 20, 20, 560, 320)
app.create
window.show(Fox::PLACEMENT_SCREEN)
status = 1
app.addTimeout(250) do
  raise 'Scintilla class registration failed' if ForkProbe.NP4_Register == 0
  begin
    status = ForkProbe.NP4_AttachAndVerify(window.xid)
  ensure
    ForkProbe.NP4_Release
    app.exit(status)
  end
end
app.run
abort "FXRuby host test failed with #{status}" unless status == 0
puts 'PASS FXRuby native HWND, fork 2067, JSON style, UTF-8 replacement and undo'
