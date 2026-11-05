# Phrase suggestion data

`phrase-suggestions.tsv` currently contains more than 3,000 unique Vietnamese
phrases, including familiar idioms and proverbs, everyday conversation, office
phrases, family expressions, names, and common collocations. The packaged data
contains no user text and stores no explanations or article passages.

The list combines prior manual curation with these additions:

- Selected Vietnamese idioms and proverbs from [VIVID](https://github.com/ReML-AI/VIVID), distributed in that repository under the MIT license. Only the phrase field is included; annotations and explanations are omitted. Entries tagged as archaic receive a lower suggestion priority.
- Selected short idiom and proverb titles from [Wikiquote's Vietnamese idiom list](https://vi.wikiquote.org/wiki/Th%C3%A0nh_ng%E1%BB%AF_Vi%E1%BB%87t_Nam). Alternate forms and clearly profane items were skipped. Wikiquote text is available under CC BY-SA; see its [reuse terms](https://vi.wikiquote.org/wiki/Wikiquote:C%C3%A2u_th%C6%B0%E1%BB%9Dng_g%E1%BA%B7p).
- New Areca-authored conversation phrases for work, family, daily chat, and dating.

The Wikiquote-derived portion requires attribution to the linked page and
share-alike terms under CC BY-SA 4.0. The phrase file is therefore a mixed-source
dataset; the repository's MIT license alone does not replace the source terms
for that portion. VIVID's MIT terms and Areca-authored entries remain separately
identified above.

Other references used during manual curation:

- Wiktionary Vietnamese idioms and proverbs: <https://vi.wiktionary.org/wiki/Th%E1%BB%83_lo%E1%BA%A1i:Th%C3%A0nh_ng%E1%BB%AF_ti%E1%BA%BFng_Vi%E1%BB%87t>
- Giải Nghĩa idiom and proverb index: <https://giainghia.com/thanh-ngu-tuc-ngu/>
- “Vào ba, ra bảy” usage reference: <https://thuvienhoidap.net/vao-ba-ra-bay-la-gi.html>
- The Trà Vinh-themed, Areca-authored phrases were checked against references on [Khmer cultural heritage and tourism in Trà Vinh](https://bvhttdl.gov.vn/tra-vinh-bao-ton-va-phat-huy-ban-sac-van-hoa-o-vung-dong-bao-dan-toc-khmer-trong-tinh-hinh-moi-20230623104744493.htm), [Trà Vinh festivals and traditional arts](https://vietnamnet.vn/hoat-dong-du-lich-gan-voi-van-hoa-khmer-da-co-nhieu-buoc-phat-trien-dang-ke-2262208.html), [Cầu Kè wax coconut](https://thuongmaibiengioimiennui.gov.vn/san-pham-vung-mien/mien-nam/2019/1/tra-vinh-trong-dua-sap-cau-ke-mo-ra-co-hoi-lam-giau-moi), [Chùa Hang](https://vietnamtourism.vn/index.php/tourism/items/1283), and [Trà Vinh food specialties](https://ngoisao.vnexpress.net/12-mon-dac-san-lam-nen-ten-tuoi-dat-tra-vinh-3345322.html). The sentences are original suggestion phrases, not copied source text.
- The Sài Gòn, Hà Nội, and Vĩnh Long-themed, Areca-authored phrases were checked against references on [Ho Chi Minh City landmarks and planning](https://www.qhkt.hochiminhcity.gov.vn/luoc-su-quy-hoach-tphcm.html), [Hanoi's autumn cốm tradition](https://sovhtt.hanoi.gov.vn/com-thuc-qua-cua-mua-thu-ha-noi/), [Vĩnh Long tourist destinations](https://vinhlong.gov.vn/du-khach/%C4%91iem-du-lich), and [the province's agricultural products](https://vioit.moit.gov.vn/vn/Printer.aspx?nId=4397). The sentences are original suggestion phrases, not copied source text.

The list is an offline phrase lexicon, not a language corpus. Corrections and
future additions should preserve the phrase's source and reuse terms.
