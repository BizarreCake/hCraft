
env = Environment(CPPPATH  = '#/include:#/include/commands',
									CCFLAGS  = '-O3',
									CXXFLAGS = '-std=c++11 -D_GLIBCXX_USE_NANOSLEEP',
									DEBUG    = True)

SConscript(['src/SConscript'], exports = 'env', variant_dir = 'build')

