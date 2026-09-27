import hashlib
import struct
import pyscrypt # Recuerda: pip install pyscrypt

# --- DATOS DE XARZA ---
# El mensaje que pusimos en chainparams.cpp
pszTimestamp = b"Xarza 07/Mar/2026: Redefiniendo la velocidad del Hash y el valor del tiempo"
genesisReward = 7.45
nTime = 1741315200 
nBits = 0x1e0ffff0
nVersion = 1

def xrz_hash(header):
    # 1. Paso Scrypt (N=1024, r=1, p=1)
    # n=1024 es 2^10
    scrypt_out = pyscrypt.hash(password=header, salt=header, N=1024, r=1, p=1, dkLen=32)
    # 2. Paso Doble SHA256 (CHash256 en C++)
    final = hashlib.sha256(hashlib.sha256(scrypt_out).digest()).digest()
    return final[::-1] # Retornamos en formato Little Endian para el Hash

def calculate_merkle_root():
    # El Merkle Root de un bloque con solo la coinbase de Xarza
    # Al tener una estructura de 2 vouts (Miner + Dev), el Merkle es fijo
    # si no cambiamos el mensaje ni la clave pública.
    # Si el programa te da un error de Merkle, copia el valor del error aquí.
    return "97ddfbbae6be97fd6cdf3e7ca13232a3afff2353e29badfab7f73011edd4ced9"

def mine():
    print("⛏️  Minando Bloque Génesis con XRZHASH...")
    merkle = calculate_merkle_root()
    target = 0x00000ffff0000000000000000000000000000000000000000000000000000000
    nonce = 0
    
    while True:
        # Header: version(4) + prev(32) + merkle(32) + time(4) + bits(4) + nonce(4)
        header = struct.pack("<i", nVersion) + (b"\x00" * 32) + \
                 bytes.fromhex(merkle)[::-1] + \
                 struct.pack("<I", nTime) + struct.pack("<I", nBits) + \
                 struct.pack("<I", nonce)
        
        res = xrz_hash(header)
        
        if int.from_bytes(res, "big") < target:
            print("\n" + "="*40)
            print("💎 ¡GÉNESIS DE XARZA ENCONTRADO! 💎")
            print("="*40)
            print(f"Nonce:  {nonce}")
            print(f"Hash:   0x{res.hex()}")
            print(f"Merkle: 0x{merkle}")
            print("="*40)
            break
            
        nonce += 1
        if nonce % 1000 == 0:
            print(f"Buscando... Nonce actual: {nonce}", end="\r")

if __name__ == "__main__":
    mine()
