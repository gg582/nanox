# EditorConfig 사용 가이드

EditorConfig는 다양한 에디터와 IDE에서 일관된 코딩 스타일을 유지할 수 있게 해주는 도구입니다. 이 문서는 EditorConfig의 사용법과 NanoX 프로젝트의 설정을 설명합니다.

## 1. EditorConfig란?

EditorConfig는 프로젝트 루트에 `.editorconfig` 파일을 두고, 이를 통해 들여쓰기 스타일, 문자 인코딩, 줄바꿈 문자 등을 자동으로 설정하는 표준입니다.

### 주요 장점

- **일관성**: 모든 개발자가 동일한 코딩 스타일을 사용
- **자동화**: 에디터가 파일을 열 때 자동으로 설정 적용
- **범용성**: 대부분의 IDE와 텍스트 에디터에서 지원

## 2. 지원 에디터 설정

### Visual Studio Code

VSCode는 기본적으로 EditorConfig를 지원합니다. 추가 확장이 필요한 경우:

```
Extension ID: EditorConfig.EditorConfig
```

### Vim/Neovim

플러그인 설치 필요:

```vim
" vim-plug 사용 시
Plug 'editorconfig/editorconfig-vim'

" packer.nvim 사용 시 (Neovim)
use 'editorconfig/editorconfig-vim'
```

### JetBrains IDEs (IntelliJ, CLion 등)

기본 내장 지원. 설정에서 활성화:
`Settings → Editor → Code Style → Enable EditorConfig support`

### Sublime Text

패키지 설치 필요:
`Package Control: Install Package → EditorConfig`

### Emacs

패키지 설치:

```elisp
;; MELPA 사용 시
(use-package editorconfig
  :ensure t
  :config
  (editorconfig-mode 1))
```

## 3. .editorconfig 문법

### 기본 구조

```ini
# 루트 표시 (상위 디렉토리 검색 중단)
root = true

# 모든 파일에 적용
[*]
charset = utf-8
end_of_line = lf

# 특정 파일 패턴에 적용
[*.c]
indent_style = tab
indent_size = 8
```

### 주요 속성

| 속성 | 설명 | 가능한 값 |
|:---|:---|:---|
| `root` | 루트 파일 여부 | `true` |
| `charset` | 문자 인코딩 | `utf-8`, `latin1`, `utf-16be`, `utf-16le` |
| `end_of_line` | 줄바꿈 문자 | `lf`, `cr`, `crlf` |
| `indent_style` | 들여쓰기 종류 | `tab`, `space` |
| `indent_size` | 들여쓰기 크기 | 숫자 (예: `2`, `4`, `8`) |
| `tab_width` | 탭 표시 너비 | 숫자 (예: `8`) |
| `trim_trailing_whitespace` | 줄 끝 공백 제거 | `true`, `false` |
| `insert_final_newline` | 파일 끝 개행 추가 | `true`, `false` |

### 파일 패턴 문법

```ini
# 단일 확장자
[*.c]

# 복수 확장자
[*.{c,h}]

# 특정 파일
[Makefile]

# 디렉토리 내 모든 파일
[lib/**.js]

# 재귀적 매칭
[**/test_*.py]
```

## 4. NanoX 프로젝트 설정

NanoX 프로젝트의 `.editorconfig`는 다음과 같은 규칙을 정의합니다:

### C 소스 파일 (`*.c`, `*.h`)

```ini
[*.{c,h}]
indent_style = tab
indent_size = 8
tab_width = 8
```

- 전통적인 Linux 커널 스타일 채택
- 탭 문자로 들여쓰기
- 탭 너비 8칸

### Makefile

```ini
[Makefile]
indent_style = tab
indent_size = 8
```

- **필수**: Makefile은 반드시 탭 문자 사용
- 스페이스로 들여쓰기 시 빌드 실패

### Markdown 문서 (`*.md`)

```ini
[*.md]
indent_style = space
indent_size = 2
trim_trailing_whitespace = false
```

- 2칸 스페이스 들여쓰기
- 줄 끝 공백 유지 (Markdown 줄바꿈에 사용)

### 설정 파일 (`*.ini`, `*.rc`)

```ini
[*.{ini,rc}]
indent_style = space
indent_size = 4
```

- 4칸 스페이스 들여쓰기
- 가독성을 위한 일관된 포맷

## 5. 새 프로젝트에 EditorConfig 적용하기

### 1단계: 파일 생성

프로젝트 루트에 `.editorconfig` 파일 생성:

```bash
touch .editorconfig
```

### 2단계: 기본 설정 추가

```ini
root = true

[*]
charset = utf-8
end_of_line = lf
insert_final_newline = true
trim_trailing_whitespace = true
```

### 3단계: 언어별 설정 추가

프로젝트에서 사용하는 언어에 맞는 설정 추가:

```ini
# Python
[*.py]
indent_style = space
indent_size = 4

# JavaScript/TypeScript
[*.{js,ts,jsx,tsx}]
indent_style = space
indent_size = 2

# C/C++
[*.{c,cpp,h,hpp}]
indent_style = tab
indent_size = 4
```

### 4단계: Git에 추가

```bash
git add .editorconfig
git commit -m "Add EditorConfig for consistent coding style"
```

## 6. 문제 해결

### 설정이 적용되지 않는 경우

1. **플러그인 확인**: 에디터에 EditorConfig 지원이 활성화되어 있는지 확인
2. **파일 위치 확인**: `.editorconfig`가 프로젝트 루트에 있는지 확인
3. **`root = true` 확인**: 상위 디렉토리의 설정이 간섭하지 않도록 설정
4. **파일 다시 열기**: 설정 변경 후 파일을 닫고 다시 열기

### 기존 코드와의 충돌

이미 다른 스타일로 작성된 코드가 있다면:

```bash
# 전체 프로젝트 재포맷 (주의해서 사용)
# 언어별 포맷터 사용 권장 (clang-format, prettier 등)
```

## 7. 참고 자료

- [EditorConfig 공식 사이트](https://editorconfig.org/)
- [EditorConfig 위키](https://github.com/editorconfig/editorconfig/wiki)
- [지원 에디터 목록](https://editorconfig.org/#pre-installed)
