# Bölüm 4: Backend Kod Üretimi, CodeGen Mimarisi ve Hedef Makine (Target Machine)

## 4.1 LLVM Backend Boru Hattı (CodeGen Pipeline)

Middle-end katmanı tarafından optimize edilmiş LLVM IR, **Backend (Arka Yüz)** katmanına girdi olarak verilir. Backend'in görevi, bu soyut IR'ı hedef donanımın (x86_64, ARM64, RISC-V, WebAssembly vb.) mimarisine özel makine komutlarına ve ikili dosyalara (.o / .obj) dönüştürmektir.

LLVM Backend boru hattı 5 ana evreden oluşur:

```
+-------------------------------------------------------------------------+
|                              LLVM IR                                    |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
| 1. Instruction Selection (SelectionDAG / GlobalISel)                   |
|    LLVM IR -> Target-Specific SelectionDAG / MachineInstr               |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
| 2. Machine Instruction Scheduling (Pre-RA Scheduling)                   |
|    Buyruk sıralaması ve pipeline gecikmelerinin minimize edilmesi       |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
| 3. Register Allocation (Yazmaç Tahsisi)                                 |
|    Sanal yazmaçların (%0, %1) fiziksel yazmaçlara (RAX, RDI) eşlenmesi  |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
| 4. Machine Instruction Scheduling (Post-RA Scheduling)                  |
|    Fiziksel yazmaç kısıtlarına göre son buyruk sıralaması              |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
| 5. Code Emission (MC Layer / llc)                                      |
|    Machine IR -> Assembly (.s) veya Nesne Dosyası (.o)                  |
+-------------------------------------------------------------------------+
```

<div class="callout callout-info">
<div class="callout-title">Backend Esnekliği</div>
Geliştirdiğiniz oyun dili için tek bir satır dahi C++ backend kodu yazmadan, sadece LLVM IR üreterek x86_64, ARM64 (Apple Silicon, Android), WebAssembly ve RISC-V mimarilerine kod çıktısı alabilirsiniz!
</div>

---

## 4.2 TableGen (`.td`) Dili ve Donanım Tanımlama

LLVM Backend'lerin en yenilikçi taraflarından biri **TableGen** altyapısıdır. Hedef işlemcinin yazmaçları, komut seti (instruction set), adresleme modları ve boru hattı (pipeline) özellikleri C++ koda elle sabitlenmek yerine `.td` uzantılı TableGen dosyalarında beyan edilir.

`llvm-tblgen` aracı bu `.td` dosyalarını okuyarak backend derlemesi sırasında otomatik C++ kodları üretir.

### Örnek TableGen Tanımı (`TargetRegister.td` konsepti)

```tablegen
// x86_64 Akümülatör Yazmacı Tanımı
def RAX : Register<"rax">;
def RBX : Register<"rbx">;

// Donanım Komutu Tanımı: ADD komutu
class x86_instruction<bits<8> opcode, string asmstr> : Instruction {
  let OutOperandList = (outs GR64:$dst);
  let InOperandList = (ins GR64:$src1, GR64:$src2);
  let AsmString = asmstr;
}

def ADD64rr : x86_instruction<0x01, "addq $src2, $dst">;
```

---

## 4.3 Buyruk Seçimi (Instruction Selection): SelectionDAG vs. GlobalISel

Buyruk Seçimi, LLVM IR talimatlarını hedef işlemcinin desteklediği somut makine komutlarıyla eşleme işlemidir.

1. **SelectionDAG (Geleneksel Yöntem):**
   * LLVM IR'ı yönlü devirsiz grafiklere (Directed Acyclic Graph - DAG) dönüştürür.
   * Düğümleri donanım komut modelleriyle eşleştirir (Pattern Matching).
   * Yüksek optimizasyonlu kod üretir ancak hafıza kullanımı fazla ve derleme süresi daha uzundur.

2. **GlobalISel (Global Instruction Selection - Modern Yöntem):**
   * Fonksiyonun tamamı üzerinde DAG oluşturmadan Doğrudan Machine IR (MIR) seviyesinde çalışır.
   * Hızlı derleme süreleri sağlar (JIT ve debug derlemeleri için idealdir).

---

## 4.4 Yazmaç Tahsisi (Register Allocation) ve Scheduling

LLVM IR sonsuz sayıda **sanal yazmaç (virtual register)** kullanabilir (`%1`, `%2`, `%3`...). Ancak gerçek işlemcilerin kısıtlı sayıda **fiziksel yazmacı (physical register)** vardır (örn: x86_64 için 16 genel amaçlı yazmaç).

### Register Allocation (RA)
Register Allocator, sanal yazmaçları fiziki yazmaçlara atar. Eğer fiziki yazmaç sayısı yetersiz kalırsa, bazı değerleri yığına saklar ve tekrar okur. Bu duruma **Spilling** adı verilir.

### Instruction Scheduling
İşlemci boru hattında (pipeline stalls) veri bağımlılıklarından kaynaklanan beklemeleri önlemek için komutların sırasını değiştirir.

<div class="callout callout-warning">
<div class="callout-title">Spilling Maliyeti</div>
Register Spilling, işlemci yazmacı yerine RAM/L1 Cache erişimi gerektirdiği için oyun gibi sıkı performans gerektiren döngülerde yavaşlamaya neden olur. LLVM Register Allocator bunu minimize edecek algoritmalar (Greedy Register Allocator) kullanır.
</div>

---

## 4.5 Machine IR (MIR) ve `llc` Aracı

**Machine IR (MIR)**, LLVM IR'ın hedef mimariye özgü komutlarla temsil edilmiş halidir. `.mir` uzantılı dosyalar halinde diske aktarılabilir ve `llc` (LLVM Static Compiler) aracı ile doğrudan incelenebilir.

```bash
# LLVM IR dosyasını x86_64 Assembly koduna dönüştürme
llc -march=x86-64 -O3 input.ll -o output.s

# LLVM IR dosyasını doğrudan Nesne Dosyasına (.o) dönüştürme
llc -filetype=obj -march=x86-64 input.ll -o output.o
```

---

## 4.6 C++ API'si İle TargetMachine Yapılandırması ve Nesne Kod Üretimi (`.o`)

Oyun derleyicimizin en son aşamasında, ürettiğimiz LLVM modülünü diskte `.o` (Object File) olarak kaydedecek C++ kodunu yazalım.

### C++ Kodu (`src/backend_codegen.cpp`)

```cpp
#include <llvm/IR/Module.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/IR/LegacyPassManager.h>
#include <system_error>

bool emitObjectFile(llvm::Module& module, const std::string& outputFilename) {
    // 1. Tüm Hedef Mimarileri İlklendir
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    // 2. Varsayılan Hedef Üçlüsünü (Target Triple) Al (Örn: x86_64-pc-linux-gnu)
    auto targetTriple = llvm::sys::getDefaultTargetTriple();
    module.setTargetTriple(targetTriple);

    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(targetTriple, error);

    if (!target) {
        llvm::errs() << "HATA: Target bulunamadi: " << error << "\n";
        return false;
    }

    // 3. Hedef Makine Yapılandırması (CPU: generic, Features: none)
    auto cpu = "generic";
    auto features = "";

    llvm::TargetOptions opt;
    auto rm = std::optional<llvm::Reloc::Model>();
    auto targetMachine = target->createTargetMachine(targetTriple, cpu, features, opt, rm);

    module.setDataLayout(targetMachine->createDataLayout());

    // 4. Çıktı Dosyasını Açma (.o)
    std::error_code ec;
    llvm::raw_fd_ostream dest(outputFilename, ec, llvm::sys::fs::OF_None);

    if (ec) {
        llvm::errs() << "HATA: Çıktı dosyası açılamadı: " << ec.message() << "\n";
        return false;
    }

    // 5. CodeGen Pass Manager İle Nesne Dosyası Üretimi
    llvm::legacy::PassManager pass;
    auto fileType = llvm::CodeGenFileType::CGFT_ObjectFile;

    if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, fileType)) {
        llvm::errs() << "HATA: TargetMachine bu dosya tipini üretemiyor!\n";
        return false;
    }

    pass.run(module);
    dest.flush();

    llvm::outs() << "BAŞARILI: Nesne dosyası üretildi -> " << outputFilename << "\n";
    return true;
}
```

### Koda Adım Adım Bakış ve Açıklaması:

1. **`InitializeAllTargets()`**: LLVM static kütüphanelerinin hedef mimari sürücülerini (x86, ARM, AArch64, MIPS vb.) C++ runtime'a kaydeder.
2. **`getDefaultTargetTriple()`**: Kodun çalıştığı bilgisayarın işletim sistemi ve işlemci mimarisini belirler (örn: `x86_64-unknown-linux-gnu`).
3. **`createTargetMachine()`**: Hedef mimarinin bellek düzenini (Data Layout) ve komut setlerini barındıran TargetMachine nesnesini inşa eder.
4. **`addPassesToEmitFile()`**: LLVM IR'ı nihai makine koduna dönüştürüp akışa yazan arka yüz pass'lerini kurar.
