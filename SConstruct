import os
import platform

AddOption('--prefix', dest='prefix', metavar='DIR', help='installation prefix')
AddOption('--libdir', dest='libdir', metavar='DIR', help='libdata dir')

opts = Variables('configure.conf')
build_dir = 'build'

opts.Add('PREFIX', default='/usr/local')
opts.Add('LIBDIR', default='/usr/local/lib')

def GetOS():
    return platform.uname()[0]

def PassEnv(name, dstname):
    if name in os.environ:
        env[dstname] = os.environ[name]
        return True
    return False

def CompilerMTOption():
    if GetOS() == 'Linux' or os == 'FreeBSD':
        return ' -pthread'
    elif GetOS() == 'SunOS':
        return ' -pthreads'
    else:
        return ''

essential_cflags = '-pipe -Wall'
cflags = '-g'
inc_path = ['.']

profile = (ARGUMENTS.get('profile') == '1')

if GetOS() == 'FreeBSD':
    inc_path.append('/usr/local/include')

if profile:
    cflags = '-O2 -mtune=generic -g -lprofiler'
    build_dir = 'profile'

env = Environment(ENV=os.environ, CPPPATH=inc_path, LIBPATH=['.'])
opts.Update(env)

if not (PassEnv('CFLAGS', 'CFLAGS') and PassEnv('CXXFLAGS', 'CXXFLAGS')):
    env.MergeFlags(cflags)
PassEnv('LDFLAGS', 'LINKFLAGS')
env.MergeFlags([essential_cflags, CompilerMTOption()])

if GetOption('prefix') is not None:
    env['PREFIX'] = GetOption('prefix')
    env['LIBDIR'] = GetOption('prefix') + '/lib'
    
if GetOption('libdir') is not None:
    env['LIBDIR'] = GetOption('libdir')

opts.Save('configure.conf', env)

Export('opts', 'env', 'GetOS')

SConscript('./SConscript', variant_dir=build_dir, duplicate=0)

# python module
SConscript('./modules/mod_python/SConscript', variant_dir=build_dir + '/modules/mod_python', duplicate=0)
# fcgi module
SConscript('./modules/mod_fcgi/SConscript', variant_dir=build_dir + '/modules/mod_fcgi', duplicate=0)

