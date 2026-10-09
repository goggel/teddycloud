"""Minimal ARD contract fixtures. No live-network dependency."""
import json
from pathlib import Path
from urllib.parse import quote
SHOW_ID = '23667030'
CORE_ID = 'urn:ard:show:0e3f60ed4ba198df'
SHOW_URL = 'https://www.ardsounds.de/sendung/example/' + CORE_ID + '/'
QUERIES = {
    'Search': 'query Search($q:String!){search(query:$q,type:ProgramSets,limit:20){programSets{nodes{id coreId title sharingUrl publicationService{title}}}}}',
    'Show': 'query Show($id:ID!,$offset:Int!,$count:Int!){programSet(id:$id){id coreId title sharingUrl items(first:$count,offset:$offset,orderBy:PUBLISH_DATE_DESC,filter:{isPublished:{equalTo:true},itemType:{notEqualTo:EVENT_LIVESTREAM}}){pageInfo{hasNextPage endCursor} nodes{id assetId title isPublished itemType publishDate duration audios{url downloadUrl allowDownload}}}}}',
    'Resolve': 'query Resolve($coreId:String!){programSetByCoreId(coreId:$coreId){id coreId title sharingUrl}}',
}
def query_url(operation, variables):
    return 'https://api.ardaudiothek.de/graphql?query=' + quote(QUERIES[operation], safe='') + '&variables=' + quote(json.dumps(variables, separators=(',', ':'), ensure_ascii=False), safe='')

def install(directory, mapping):
    fixture_root = Path(__file__).parent / 'fixtures' / 'podcasts' / 'ard'
    show = json.loads((fixture_root / 'ard-show.json').read_text())['data']['programSet']
    def record(name, operation, variables, value):
        path = directory / name
        path.write_text(json.dumps(value))
        mapping[query_url(operation, variables)] = str(path)
    record('ard-search.json', 'Search', {'q': 'ARD "&ä'}, {'data': {'search': {'programSets': {'nodes': [show]}}}})
    record('ard-resolve.json', 'Resolve', {'coreId': CORE_ID}, {'data': {'programSetByCoreId': show}})
    record('ard-show.json', 'Show', {'id': SHOW_ID, 'offset': 0, 'count': 0}, {'data': {'programSet': show}})
    for offset in [0, 50]:
        value = json.loads((fixture_root / f'ard-page-{offset}.json').read_text())
        record(f'ard-page-{offset}.json', 'Show', {'id': SHOW_ID, 'offset': offset, 'count': 50}, value)
    return show
