import hashlib
import tempfile
import unittest
from unittest.mock import patch
import RNS
from tools.tracks.application import TrackApplication
from tools.tracks.blob_store import BlobStore
from tools.tracks.directory_index import DirectoryIndex
from tools.tracks.protocol import packed, unpacked
from tools.tracks.region_catalog import RegionCatalog


class MetadataQueryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.author, self.directory = RNS.Identity(), RNS.Identity()
        self.blobs = BlobStore(self.temporary.name)
        self.addCleanup(self.blobs.close)
        self.index = DirectoryIndex(self.temporary.name, self.blobs)
        self.addCleanup(lambda: self.index.close())
        self.data = b'<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><Point><coordinates>0.5,0.5</coordinates></Point></Placemark></kml>'
        self.digest = hashlib.sha256(self.data).digest()
        upload = self.blobs.begin(self.author.hash, self.digest, len(self.data))
        self.blobs.append(self.author.hash, upload['token'], 0, self.data)
        self.blobs.finish(self.author.hash, upload['token'])
        ring = [[0,0],[1,0],[1,1],[0,1],[0,0]]
        self.catalog = RegionCatalog('test', {'type':'FeatureCollection','features':[{'type':'Feature','id':'r','properties':{'name':'Region'},'geometry':{'type':'Polygon','coordinates':[ring]}}]})
        self.app = TrackApplication(self.directory, self.blobs, self.index, regions=self.catalog)
        self.box = [0,0,10000000,10000000]

    def publish(self, nonce, activity=3, tags=None, version=2):
        record = [version,self.author.get_public_key(),bytes([nonce])*16,0,1,None,0,100,100,'Track','',[5000000,5000000,5000000,5000000],[0,None,None,1],[self.digest,len(self.data),'application/vnd.google-earth.kml+xml',1]]
        if version == 2:
            record.extend([[activity, tags or [],0,0,1,0,0,None],None])
        raw = packed(record)
        return self.index.publish(self.author.hash,[raw,self.author.sign(f'TrailMate.Track.Manifest.v{version}\0'.encode()+raw)],None), record

    def request(self, operation, body):
        return unpacked(self.app.handle(self.author.hash,packed([1,0,operation,b'r'*16,8192,body])))

    def test_map_exact_all_tags_activity_and_legacy_summary(self):
        hit, _ = self.publish(1,tags=['山脊','trail, ridge'])
        self.publish(2,tags=['山脊'])
        self.publish(3,activity=1,tags=['山脊','trail, ridge'])
        old, _ = self.publish(4,version=1)
        self.assertEqual(self.request(0,[])[5][7],list(range(9)))
        self.assertTrue({14,15,16}.issubset(self.request(0,[2])[5][7]))
        page = self.request(15,[self.box,None,None,0,3,['山脊','trail, ridge'],16,None,None])
        self.assertEqual(page[4],200)
        self.assertEqual([row[0] for row in page[5][2]],[hit['object']])
        self.assertEqual(page[5][2][0][10:],[3,['山脊','trail, ridge']])
        self.assertEqual(self.request(15,[self.box,None,None,0,3,['Trail, ridge'],16,None,None])[5][2],[])
        self.assertEqual(self.request(15,[self.box,None,None,0,0,[],16,None,None])[5][2][0][0],old['object'])
        self.assertTrue(all(len(row)==10 for row in self.request(1,[self.box,None,None,0,16,None,None])[5][2]))

    def test_filters_cursor_snapshot_restart_and_invalid_input(self):
        for nonce in range(1,4):
            self.publish(nonce,activity=250,tags=['山脊'])
        first = self.index.query(self.box,activity=250,tags=['山脊'],metadata=True,limit=1)
        second = self.index.query(self.box,activity=250,tags=['山脊'],metadata=True,limit=1,snapshot=first['snapshot'],cursor=first['cursor'])
        self.assertNotEqual(first['items'][0][0],second['items'][0][0])
        for changes in [{'activity':3},{'tags':['different']},{'metadata':False,'activity':None,'tags':[]}]:
            options={'activity':250,'tags':['山脊'],'metadata':True,'snapshot':first['snapshot'],'cursor':first['cursor']};options.update(changes)
            with self.assertRaises(ValueError):
                self.index.query(self.box,**options)
        self.index.close();self.index=DirectoryIndex(self.temporary.name,self.blobs)
        self.assertEqual(len(self.index.query(self.box,activity=250,tags=['山脊'],metadata=True)['items']),3)
        with self.assertRaises(ValueError):
            self.index.query(self.box,activity=True,tags=[],metadata=True)
        with self.assertRaisesRegex(ValueError,'CMP1'):
            self.request(15,[self.box,None,None,0,True,[],16,None,None])
        for activity,tags in [(256,[]),(3,['山'*17]),(3,['x','x']),(3,['x\ny']),(3,'x')]:
            self.assertEqual(self.request(15,[self.box,None,None,0,activity,tags,16,None,None])[4],422)

    def test_region_empty_pages_keep_cursor_and_skip_nonmatching_file_reads(self):
        records={}
        for nonce in range(1,36):
            published,record=self.publish(nonce,tags=[]);records[published['object']]=(published,record)
        published,record=records[max(records)]
        record[4],record[5],record[8],record[14][1]=2,published['hash'],101,['last']
        raw=packed(record);self.index.publish(self.author.hash,[raw,self.author.sign(b'TrailMate.Track.Manifest.v2\0'+raw)],published['hash'])
        snapshot,cursor,found,pages=None,None,[],0
        with patch.object(self.blobs,'read_geometries',wraps=self.blobs.read_geometries) as reads:
            while True:
                page=self.request(16,['test:r',self.catalog.version,'intersects',None,None,0,3,['last'],16,snapshot,cursor])
                self.assertEqual(page[4],200);snapshot,cursor,items=page[5];pages+=1;found.extend(items)
                if pages==1:self.assertEqual(items,[]);self.assertIsNotNone(cursor)
                if cursor is None:break
            self.assertEqual(reads.call_count,1)
        self.assertGreaterEqual(pages,3)
        self.assertEqual([row[0] for row in found],[published['object']])

    def test_new_head_changes_filters_and_archive_state(self):
        published,record=self.publish(1,tags=['old'])
        record[4],record[5],record[8],record[14][0],record[14][1]=2,published['hash'],101,1,['new']
        raw=packed(record);updated=self.index.publish(self.author.hash,[raw,self.author.sign(b'TrailMate.Track.Manifest.v2\0'+raw)],published['hash'])
        self.assertEqual(self.index.query(self.box,tags=['old'],metadata=True)['items'],[])
        record[4],record[5],record[6],record[8]=3,updated['hash'],1,102
        raw=packed(record);self.index.publish(self.author.hash,[raw,self.author.sign(b'TrailMate.Track.Manifest.v2\0'+raw)],updated['hash'])
        self.assertEqual(self.index.query(self.box,tags=['new'],metadata=True)['items'],[])
        self.assertEqual(len(self.index.query(self.box,tags=['new'],state=None,metadata=True)['items']),1)


if __name__ == '__main__':
    unittest.main()
