# FBI-KR

FBI-KR은 3DS를 위한 오픈소스 타이틀 관리자의 한국어 버전입니다.

 * 텍스트가 있는 스크립트는 sources 폴더에 있습니다. 그런데 스크립트 내에 텍스트가 허드코딩 되어 있어 텍스트를 찾기 어려운 관계로 일부 부분은 미번역 상태로 남아있을 수 있습니다. 혹시나 스크립트에서 미번역 부분을 발견하셨다면 Pull request를 통해 기여해주세요! 스크립트를 잘 모르시더라도 issue를 생성해서 정확히 어느 부분에서 번역 오류/미반약이 됐는지 알려주세요.
 * 현재 확인된 미번역 부분: 시간 표시, 예/아니오 버튼

다운로드(원본): https://github.com/Steveice10/FBI/releases

다운로드(KR): https://github.com/lkdjfalnnlvz/FBI-KR/releases

빌드하려면 [devkitARM](http://sourceforge.net/projects/devkitpro/files/devkitARM/), 그리고 devkitPro pacman repository에 있는 3ds-curl, 3ds-zlib, 3ds-jansson이 필요합니다.

# 기능

* SD 카드, TWL 사진, TWL 사운드, 저장 데이터 및 확장 저장 데이터를 탐색하고 수정합니다.
* DS 카트리지에서 세이브 데이터를 추출, 주입, 삭제합니다.
* 세이브 데이터 보안 값을 추출, 주입, 삭제합니다.
* 타이틀/티켓을 파일 시스템과 로컬 네트워크를 통해, 또한 URL이나 QR코드로 인터넷을 통해 설치합니다.
  * 설치할 때 타이틀 시드를 자동으로 주입합니다, 인터넷 또는 SD카드를 통한 방법 둘 다.
* 보류 중인 타이틀 확인 및 삭제 (다운로드된 업데이트, 진행중인 e샵 타이틀, 등등).
* "sdmc:/fbi/theme/"에 RomFS 리소스에 대한 교체 항목을 배치하여 테마를 커스텀합니다.

* Luma3DS에서 CIA, 3DS 또는 3DSX에서 실행할 때만 사용 가능:
  * CTR NAND, TWL NAND, 그리고 시스템 세이브 데이터 확인 및 삭제.
  * SD카드에 raw NAND 이미지 덤프
  * 시스템에 설치된 타이틀 실행

# Credit

Banner: Originally created by [OctopusRift](http://gbatemp.net/members/octopusrift.356526/), touched up by [Apache Thunder](https://gbatemp.net/members/apache-thunder.105648/), updated for new logo by [PabloMK7](http://gbatemp.net/members/pablomk7.345712/).

Logo: [PabloMK7](http://gbatemp.net/members/pablomk7.345712/)

SPI Protocol Information: [TuxSH](https://github.com/TuxSH/) ([TWLSaveTool](https://github.com/TuxSH/TWLSaveTool))
