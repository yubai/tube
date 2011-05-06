import os
import platform

AddOption('--prefix', dest='prefix', metavar='DIR', help='installation prefix')
AddOption('--libdir', dest='libdir', metavar='DIR', help='libdata dir')

opts = Variables('configure.conf')
opts.Add('PREFIX', default='/usr/local')
opts.Add('LIBDIR', default='/usr/local/lib')

def GetOS():
    return platform.uname()[0]

def PassEnv(name, dstname):
    if name in os.environ:
        env[dstname] = os.environ[name]

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

profile = (ARGUMENTS.get('profile', 0) == '1')

if GetOS() == 'FreeBSD':
    inc_path.append('/usr/local/include')

if profile:
    cflags = '-O2 -mtune=generic -g -lprofiler'

env = Environment(ENV=os.environ, CPPPATH=inc_path)
opts.Update(env)

env.MergeFlags(cflags)
PassEnv('CFLAGS', 'CFLAGS')
PassEnv('CXXFLAGS', 'CXXFLAGS')
PassEnv('LDFLAGS', 'LINKFLAGS')
env.MergeFlags([essential_cflags, CompilerMTOption()])

if GetOption('prefix') is not None:
    env['PREFIX'] = GetOption('prefix')
    env['LIBDIR'] = GetOption('prefix') + '/lib'
    
if GetOption('libdir') is not None:
    env['LIBDIR'] = GetOption('libdir')

opts.Save('configure.conf', env)

Export('opts', 'env', 'GetOS')

build_dir = 'build'
if profile:
    build_dir = 'profile'

SConscript('./SConscript', variant_dir=build_dir, duplicate=0)
SConscript('./modules/mod_python/SConscript', variant_dir=build_dir + '/modules/mod_python', duplicate=0)

