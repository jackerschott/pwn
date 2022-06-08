project('pwn', 'c', version : '1.0', license : 'GPLv3')

datadir = get_option('prefix') / get_option('datadir') / 'pwn/'
add_global_arguments('-DDATADIR="' + datadir + '"', language : 'c')

src = files([
	'src/audioh.c',
	'src/draw.c',
	'src/game.c',
	'src/gfxh.c',
	'src/main.c',
	'src/notation.c',
	'src/util.c'
])
inc = include_directories('src', 'src/minimp3')

dpthread = dependency('threads')
dcairo = dependency('cairo')
dx11 = dependency('x11')
dpulse = dependency('libpulse')
dpulsesimple = dependency('libpulse-simple')

executable('pwn', src, include_directories : inc,
	dependencies : [dpthread, dcairo, dx11, dpulse, dpulsesimple],
	install : true)
install_man('pwn.1')
install_subdir('sounds', install_dir : get_option('datadir') / 'pwn')

inc = include_directories('test', 'src')
src = files(['test/gametest/gametest.c', 'src/game.c', 'src/notation.c'])
exe = executable('testgame', src, include_directories : inc)
test('testgame', exe)

inc = include_directories('test', 'src')
src = files(['test/notationtest/notationtest.c', 'src/notation.c'])
exe = executable('testnotation', src, include_directories : inc)
test('testnotation', exe)
