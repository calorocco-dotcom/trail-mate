"""Directory publication receipts, independently verifiable after transport."""
import RNS
from tools.tracks.protocol import binary, integer, packed, unpacked, verify_manifest
DOMAIN = b"TrailMate.Track.Receipt.v1\0"


def sign_receipt(identity, epoch, request, verified, accepted_at):
    if not binary(epoch,16) or not binary(request,16) or not integer(accepted_at,0,253402300799):
        raise ValueError("receipt context")
    record=verified["record"]
    delivery=RNS.Destination.hash_from_name_and_identity("lxmf.delivery",identity)
    raw=packed([1,request,verified["object_id"],verified["revision_hash"],record[4],record[6],record[13][0],record[13][1],
                identity.get_public_key(),delivery,epoch,1,accepted_at])
    return [raw,identity.sign(DOMAIN+raw)]


def verify_receipt(signed, directory_public, epoch, request, manifest_signed):
    if type(signed) is not list or len(signed)!=2 or not binary(signed[1],64) or not binary(directory_public,64) or not binary(epoch,16) or not binary(request,16):
        raise ValueError("receipt shape")
    record=unpacked(signed[0],1024)
    if type(record) is not list or len(record)!=13 or record[0]!=1 or record[11]!=1 or not integer(record[12],0,253402300799):
        raise ValueError("receipt fields")
    directory=RNS.Identity(create_keys=False)
    if not directory.load_public_key(directory_public):
        raise ValueError("directory key")
    verified=verify_manifest(manifest_signed)
    manifest=verified["record"]
    expected=[1,request,verified["object_id"],verified["revision_hash"],manifest[4],manifest[6],manifest[13][0],manifest[13][1],
              directory_public,RNS.Destination.hash_from_name_and_identity("lxmf.delivery",directory),epoch,1]
    if record[:12]!=expected or not directory.validate(signed[1],DOMAIN+signed[0]):
        raise ValueError("receipt signature/binding")
    return record
