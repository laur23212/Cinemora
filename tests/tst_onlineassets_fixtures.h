// Canned provider responses for tst_onlineassets. A separate header because moc cannot
// parse multi-line raw string literals in the file it generates code for.
#pragma once

namespace fixtures {

const char kPexelsPage[] = R"({"page":1,"per_page":2,"total_results":40,
            "next_page":"https://api.pexels.com/v1/videos/search?page=2",
            "videos":[{"id":7,"width":3840,"height":2160,"duration":12,
              "url":"https://www.pexels.com/video/a-dog-on-a-beach-123456/","image":"https://img/t.jpg",
              "user":{"name":"Ann","url":"https://www.pexels.com/@ann"},
              "video_files":[{"width":3840,"height":2160,"file_type":"video/mp4","link":"https://v/4k.mp4"},
                             {"width":640,"height":360,"file_type":"video/mp4","link":"https://v/sd.mp4"},
                             {"width":1920,"height":1080,"file_type":"video/mp4","link":"https://v/hd.mp4"}]}]})";

const char kPixabayPage[] = R"({"total":3,"totalHits":3,"hits":[
            {"id":1,"pageURL":"https://pixabay.com/videos/id-1/","tags":"sea, waves, beach, sun","duration":9,"user":"bob","user_id":5,
             "videos":{"large":{"url":"","width":0,"height":0,"size":0,"thumbnail":""},
                       "medium":{"url":"https://p/m.mp4","width":1280,"height":720,"size":100,"thumbnail":"https://p/m.jpg"},
                       "small":{"url":"https://p/s.mp4","width":960,"height":540,"size":50,"thumbnail":"https://p/s.jpg"},
                       "tiny":{"url":"https://p/t.mp4","width":640,"height":360,"size":10,"thumbnail":"https://p/t.jpg"}}}]})";

const char kPixabayEmpty[] = R"({"totalHits":2,"hits":[]})";

const char kFreesoundPage[] = R"({"count":1,"next":"https://freesound.org/apiv2/search/text/?page=2","results":[
            {"id":42,"name":"door slam.wav","username":"foley","duration":1.5,"license":"https://creativecommons.org/licenses/by/4.0/",
             "previews":{"preview-hq-mp3":"https://f/hq.mp3","preview-lq-mp3":"https://f/lq.mp3"},
             "images":{"waveform_m":"https://f/w.png"},"url":"https://freesound.org/s/42/","type":"wav"}]})";

const char kJamendoPage[] = R"({"headers":{"status":"success","code":0,"results_count":2},"results":[
            {"id":"1","name":"Allowed","duration":"120","artist_name":"A","image":"https://j/1.jpg","audio":"https://j/1.mp3",
             "audiodownload":"https://j/1-dl.mp3","audiodownload_allowed":true,"license_ccurl":"http://creativecommons.org/licenses/by-sa/3.0/"},
            {"id":"2","name":"Blocked","duration":"90","artist_name":"B","audio":"https://j/2.mp3","audiodownload":"https://j/2-dl.mp3",
             "audiodownload_allowed":false,"license_ccurl":"http://creativecommons.org/licenses/by-nc/3.0/"}]})";

const char kJamendoFailed[] = R"({"headers":{"status":"failed","code":5,"error_message":"Your credential is not authorized."},"results":[]})";

const char kOpenversePage[] = R"({"result_count":30,"page_count":2,"page_size":20,"page":1,"results":[
            {"id":"abc","title":"Rain","creator":"Kim","url":"https://o/rain.mp3","thumbnail":null,"duration":61500,
             "license":"by","license_version":"4.0","license_url":"https://creativecommons.org/licenses/by/4.0/",
             "attribution":"\"Rain\" by Kim is licensed under CC BY 4.0.","foreign_landing_url":"https://freesound.org/s/1/","category":"sound_effect"}]})";

const char kDafinIndex[] = R"({"version":1,"items":[
            {"id":"heart","kind":"sticker","title":"Heart pop","tags":"love","file":"stickers/heart.json","license":"CC0 1.0"},
            {"id":"lt","kind":"title","title":"Lower third","tags":"name","file":"titles/lt.json","editableText":true},
            {"id":"remote","kind":"sticker","title":"Wave","file":"https://raw.example/wave.json","license":"Apache-2.0"},
            {"id":"bad","kind":"nope","file":"x.json"}]})";

const char kCatalogItem[] = R"({"files":[{"width":3840,"link":"4k"},{"width":640,"link":"sd"},{"width":1920,"link":"hd"}],
                                       "user":{"name":"Ann"}})";

const char kMiniLottie[] = R"({"v":"5.7.4","fr":30,"ip":0,"op":30,"w":64,"h":64,"layers":[]})";

const char kNotLottie[] = R"({"html":"<p>not an animation</p>"})";

const char kNewerSchema[] = R"({"schemaVersion":99,"assets":[{"key":"x:1","localPath":"/tmp"}]})";

} // namespace fixtures
