"""Minimal reader for PyTorch zip checkpoints without torch (stdlib + numpy)."""
import zipfile, pickle, numpy as np, collections

DT = {'FloatStorage': np.float32, 'HalfStorage': np.float16, 'LongStorage': np.int64,
      'IntStorage': np.int32, 'BFloat16Storage': None, 'DoubleStorage': np.float64,
      'ByteStorage': np.uint8, 'BoolStorage': np.bool_}

class _Stor:
    def __init__(self, name): self.name = name

def load(path):
    z = zipfile.ZipFile(path)
    names = z.namelist()
    root = names[0].split('/')[0]
    pk = [n for n in names if n.endswith('data.pkl')][0]
    prefix = pk[:-len('data.pkl')]
    cache = {}
    def storage(key, dtype):
        if key not in cache:
            raw = z.read(prefix + 'data/' + key)
            cache[key] = np.frombuffer(raw, dtype=dtype)
        return cache[key]
    def rebuild(stor, offset, size, stride, *a):
        dtype, key = stor
        buf = storage(key, dtype)
        size = tuple(size); stride = tuple(stride)
        if len(size) == 0:
            return buf[offset:offset+1].reshape(()).copy()
        it = buf.itemsize
        return np.lib.stride_tricks.as_strided(buf[offset:], shape=size,
                                               strides=[s*it for s in stride]).copy()
    class U(pickle.Unpickler):
        def find_class(self, mod, name):
            if name == '_rebuild_tensor_v2': return rebuild
            if name == '_rebuild_parameter': return lambda d, rg, bh: d
            if name in DT: return name
            if mod == 'collections' and name == 'OrderedDict': return collections.OrderedDict
            if name == '_rebuild_tensor': return rebuild
            raise pickle.UnpicklingError('refused %s.%s' % (mod, name))
        def persistent_load(self, pid):
            typ, sname, key, loc, n = pid
            return (DT[sname], key)
    return U(z.open(pk)).load()
