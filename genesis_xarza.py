import hashlib
import struct
import sys
import time
import multiprocessing
from multiprocessing import Value

# Intentar importar pyscrypt
try:
    import pyscrypt
except ImportError:
    print("\n❌ Error: No se encontró la librería 'pyscrypt'.")
    sys.exit(1)

# =============================================================================
# CONFIGURACIÓN DE XARZA
# =============================================================================
pszTimestamp = b"Xarza 07/Mar/2026: Redefiniendo la velocidad del Hash y el valor del tiempo"
pubkey = "040184710fa689ad5023690c80f3a49c8f13f8d45b8c857fbcbc8bc4a8e4d3eb4b10f4d4604fa08dce601aaf0f470216fe1b51850b4acf21b179c45070ac7b03a9"
nTime = 1741315200
nBits = 0x1f00ffff  # Dificultad Baja
nReward = 7.45
nVersion = 1

def calculate_merkle_root():
    """Cálculo estándar de Merkle Root"""
    tx = struct.pack("<I", 1) 
    tx += struct.pack("<B", 1) 
    tx += b"\x00" * 32 + struct.pack("<I", 0xFFFFFFFF) 
    
    script_sig = struct.pack("<I", 486604799) + b"\x01\x04" + struct.pack("<B", len(pszTimestamp)) + pszTimestamp
    tx += struct.pack("<B", len(script_sig)) + script_sig
    
    tx += struct.pack("<I", 0xFFFFFFFF) 
    tx += struct.pack("<B", 1) 
    tx += struct.pack("<q", int(nReward * 100000000)) 
    
    script_pubkey = bytes.fromhex(pubkey)
    full_script = b"\x41" + script_pubkey + b"\xac" 
    tx += struct.pack("<B", len(full_script)) + full_script
    
    tx += struct.pack("<I", 0) 
    
    return hashlib.sha256(hashlib.sha256(tx).digest()).digest().hex()

def xrz_hash(header):
    """Algoritmo de Minería: Scrypt + Doble SHA256"""
    s_hash = pyscrypt.hash(password=header, salt=header, N=1024, r=1, p=1, dkLen=32)
    final = hashlib.sha256(hashlib.sha256(s_hash).digest()).digest()
    return final[::-1]

def mining_worker(start, step, target, header_base, found_event, result_dict, hash_counter):
    nonce = start
    local_count = 0
    while not found_event.is_set():
        header = header_base + struct.pack("<I", nonce)
        res = xrz_hash(header) # PoW Hash (Scrypt)
        
        local_count += 1
        if local_count >= 50:
            with hash_counter.get_lock():
                hash_counter.value += local_count
            local_count = 0

        if int.from_bytes(res, "big") < target:
            result_dict['nonce'] = nonce
            
            # [LA MAGIA AQUI] C++ espera el Block Hash normal (Doble SHA256), NO el PoW Hash
            block_hash = hashlib.sha256(hashlib.sha256(header).digest()).digest()[::-1].hex()
            
            result_dict['hash'] = block_hash
            found_event.set()
            break
        nonce += step

def mine():
    print("\n" + "="*60)
    print("🚀 XARZA GÉNESIS MINER (CORRECCIÓN DE HASH PARA C++)")
    print("="*60)
    
    merkle = calculate_merkle_root()
    print(f"📦 Merkle Root: {merkle}")
    
    target = 0x00ffff0000000000000000000000000000000000000000000000000000000000
    
    header_base = struct.pack("<i", nVersion) + (b"\x00" * 32) + \
                 bytes.fromhex(merkle)[::-1] + \
                 struct.pack("<I", nTime) + struct.pack("<I", nBits)
    
    cpus = multiprocessing.cpu_count()
    print(f"⛏️  Buscando hash con {cpus} núcleos...")
    
    found_event = multiprocessing.Event()
    manager = multiprocessing.Manager()
    result_dict = manager.dict()
    hash_counter = Value('q', 0)
    
    processes = []
    for i in range(cpus):
        p = multiprocessing.Process(target=mining_worker, args=(i, cpus, target, header_base, found_event, result_dict, hash_counter))
        processes.append(p)
        p.start()
    
    try:
        while not found_event.is_set(): 
            time.sleep(1)
        for p in processes: p.join()
        
        print("\n\n" + "—"*60)
        print("💎 ¡EL HASH DEFINITIVO ENCONTRADO! 💎")
        print("—"*60)
        print(f"Nonce:  {result_dict['nonce']}")
        print(f"Hash:   0x{result_dict['hash']}  <--- ¡PON ESTO EN EL ASSERT!")
        print(f"Merkle: 0x{merkle}")
        print("—"*60)

    except KeyboardInterrupt:
        print("\nMinería detenida.")
        found_event.set()
        for p in processes: p.terminate()

if __name__ == "__main__":
    multiprocessing.freeze_support()
    mine()
