# Bölüm 5: İleri Düzey Konular: ORC JIT, MLIR, Shader Kod Üretimi ve Derleyici Araçları

## 5.1 LLVM ORC JIT (On-Request Compilation) Mimarisi ve Oyun İçi Betik (Scripting) Çalıştırma

Oyun motorlarında canlı kod yenileme (Hot-Reloading) ve hızlı betik (scripting) çalıştırma kritik gereksinimlerdir. Geleneksel olarak Lua veya C# gibi diller yorumlanarak (interpreted) veya sanal makine üzerinde çalıştırılır. LLVM **ORC JIT (On-Request Compilation)** API'si sayesinde, oyun içinde yazılan betik kodları çalışma zamanında milisaniyeler içinde doğrudan **öznel makine kodına** derlenir ve sıfır performans kaybı ile çalıştırılır.

ORC JIT mimarisinin ana bileşenleri şunlardır:
1. **`ExecutionSession`:** Tüm JIT durumunu, thread'leri ve sembol tablolarını yönetir.
2. **`JITDylib`:** Dinamik kütüphane benzeri sembol konteyneridir. Oyun motorunuzdaki C++ fonksiyonlarını JIT tarafına kaydetmenizi sağlar.
3. **`JITLink`:** Bellek içi nesne dosyalarını bağlayan ve sayfa izinlerini (Executable/Writable) ayarlayan ultra hızlı bağlayıcıdır.

```
+-----------------------------------------------------------------------+
|                      Oyun İçi Script / Kod                            |
+-----------------------------------------------------------------------+
                                   |
                                   v
+-----------------------------------------------------------------------+
|                AST & LLVM IR Generation (Memory)                      |
+-----------------------------------------------------------------------+
                                   |
                                   v
+-----------------------------------------------------------------------+
|            LLVM ORC JIT (LLJIT / ThreadSafeModule)                    |
+-----------------------------------------------------------------------+
                                   |
                                   v
+-----------------------------------------------------------------------+
|  JITLink -> Yerel Bellek (RAM) -> Fonksiyon Pointer (C++ Call)        |
+-----------------------------------------------------------------------+
```

<div class="callout callout-info">
<div class="callout-title">JIT Scripting Avantajı</div>
Oyun içi düşman yapay zekası (AI) veya fizik hesapları JIT ile derlendiğinde, yorumlanan Lua koduna kıyasla 10 kat ile 50 kat arasında performans artışı elde edilir.
</div>

### C++ Kodu: Çalışma Zamanında Betik Çalıştıran JIT Motoru (`src/jit_engine.cpp`)

```cpp
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <iostream>

// Oyun İçi Script Tarafından Çağrılacak C++ Fonksiyonu (Host Engine Function)
extern "C" void GameEngine_LogMessage(const char* msg) {
    std::cout << "[Oyun Motoru JIT Log]: " << msg << std::endl;
}

void runJITScript() {
    // 1. LLVM Target Sistemlerini Başlat
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    // 2. LLJIT Örneği Oluşturma
    auto jitOrErr = llvm::orc::LLJITBuilder().create();
    if (!jitOrErr) {
        llvm::errs() << "JIT Oluşturulamadı!\n";
        return;
    }
    auto jit = std::move(*jitOrErr);

    // 3. Thread-Safe LLVM Modülü ve Context Yapılandırması
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("JITScriptModule", *context);
    llvm::IRBuilder<> builder(*context);

    // 4. C++ Tarafındaki Log Message Fonksiyonunu JIT Modülüne Tanımlama
    auto* logFuncType = llvm::FunctionType::get(builder.getVoidTy(), {builder.getPtrTy()}, false);
    auto* logFunc = llvm::Function::Create(logFuncType, llvm::Function::ExternalLinkage, "GameEngine_LogMessage", module.get());

    // 5. JIT Script Fonksiyonu Oluşturma: void RunScript()
    auto* scriptFuncType = llvm::FunctionType::get(builder.getVoidTy(), false);
    auto* scriptFunc = llvm::Function::Create(scriptFuncType, llvm::Function::ExternalLinkage, "RunScript", module.get());

    auto* bb = llvm::BasicBlock::Create(*context, "entry", scriptFunc);
    builder.SetInsertPoint(bb);

    // Metin Sabiti Oluşturma
    auto* strVal = builder.CreateGlobalString("LLVM ORC JIT Script Basariyla Calisti!", "script_str");
    builder.CreateCall(logFunc, {strVal});
    builder.CreateRetVoid();

    // 6. Modülü JIT'e Ekleme
    auto tsm = llvm::orc::ThreadSafeModule(std::move(module), std::move(context));
    cantFail(jit->addIRModule(std::move(tsm)));

    // 7. JIT İçindeki "RunScript" Sembolünü Bulma ve Çalıştırma
    auto symOrErr = jit->lookup("RunScript");
    if (!symOrErr) {
        llvm::errs() << "JIT Sembolü Bulunamadı!\n";
        return;
    }

    // Sembolü C++ Fonksiyon Göstericisine (Function Pointer) Cast Etme
    void (*scriptFn)() = symOrErr->toPtr<void (*)()>();

    // 8. KODU YEREL HIZDA ÇALIŞTIR!
    scriptFn();
}
```

---

### Koda Adım Adım Derinlemesine Bakış ve JIT Yürütme Algoritması

1. **`LLJITBuilder().create()`:**
   * *Çalışma Mantığı:* Bilgisayarın yerel CPU mimarisini tespit eder, JITLink ve bellek sayfa yöneticisini (Memory Page Manager: Read/Write/Execute izinleri) başlatır.
2. **`ThreadSafeModule` (TSM) Sarmalayıcısı:**
   * *Çalışma Mantığı:* `Module` ve `LLVMContext` nesnelerini kilit mekanizması altında birleştirir. Böylece oyun motorunun arka plan iş parçacıkları (Worker Threads) güvenle derleme yapabilir.
3. **Ev Sahibi Sembol Bağlama (`GameEngine_LogMessage`):**
   * *Çalışma Mantığı:* C++ tarafında `extern "C"` olarak tanımlanan fonksiyon, JIT sembol tablosuna kaydedilir. JIT derleyicisi bu sembolün bellek adresini oyun motorunun proses adres alanından (Process Address Space) okur.
4. **`symOrErr->toPtr<void (*)()>()` Fonksiyon Göstericisi Dönüşümü:**
   * *Çalışma Mantığı:* JIT tarafından RAM'de derlenen makine kodunun başlangıç adresini C++ fonksiyon göstericisine (`function pointer`) dökümler. `scriptFn()` çağrısı, araya hiçbir sanal makine veya yorumlayıcı katmanı girmeden doğrudan CPU seviyesinde yürütülür.

---

## 5.2 MLIR (Multi-Level Intermediate Representation) ve Oyun Motorları İçin Önemi

LLVM IR harika bir alt seviye temsil olsa da, yüksek seviyeli matematiksel yapılar (matris çarpımları, GPU thread blokları, oyun içi fizik simülasyonları) LLVM IR seviyesine indirildiğinde kaybolur.

**MLIR (Multi-Level Intermediate Representation)**, LLVM projesinin çok seviyeli ara temsil altyapısıdır. **Dialect (Diyalekt)** adı verilen modüler yapıları sayesinde:
* **`Affine` Dialect:** Döngü optimizasyonları ve bellek erişim desenleri için.
* **`Vector` / `Linalg` Dialect:** Matrix/Vector işlemleri ve SIMD vektörizasyonu için.
* **`GPU` Dialect:** CUDA, ROCm ve Vulkan Compute Kernel yönetimi için.

Oyun dilinizde önce MLIR seviyesinde matris ve vektör optimizasyonları yapabilir, ardından kodu LLVM IR'a indirgeyerek (lowering) donanıma gönderebilirsiniz.

---

## 5.3 LLVM IR'dan SPIR-V Üretimi ve DXC (DirectX Shader Compiler)

Modern oyun motorları grafik kartlarında (GPU) paralel hesaplama yapmak için SPIR-V (Vulkan) ve DXIL (DirectX 12) formatlarına ihtiyaç duyar.

* **LLVM SPIR-V Target / Backend:** Oyun dilinizden ürettiğiniz LLVM IR'ı `llvm-spirv` aracı veya kütüphanesi aracılığıyla doğrudan SPIR-V ikili formatına çevirebilirsiniz. Bu sayede hem CPU hem GPU kodunu **aynı dille** yazabilirsiniz!
* **DXC (DirectX Shader Compiler):** Microsoft'un açık kaynaklı derleyicisidir. LLVM tabanlıdır ve HLSL shader kodlarını SPIR-V veya DXIL formatına dönüştürür.

---

## 5.4 Clang Tooling, LLD ve Clang Sanitizer Araçları

Bir oyun programlama dili ekosistemi geliştirmek yalnızca derleyici yazmaktan ibaret değildir; geliştirici araçlarına (Developer Tooling) da ihtiyaç vardır.

1. **Clang Tooling:** Oyun diliniz için otomatik kod biçimlendirici (Formatter), statik analiz araçları (Linter) ve Otomatik Tamamlama (Language Server Protocol - LSP) yazmanızı sağlar.
2. **LLD (LLVM Linker):** Oyun projenizin bağımlılıklarını bağlarken MSVC `link.exe` veya GNU `ld` yerine LLD kullanarak bağlama (linking) sürelerini 5-10 kat kısaltabilirsiniz.
3. **Clang Sanitizer Araçları:** Oyun motorunuzda bellek sızıntılarını ve paralel izlek (multithreading) hatalarını yakalamak için LLVM Sanitize altyapısı entegre edilmelidir:
   * **ASan (AddressSanitizer):** Yığın ve bellek taşmalarını (buffer overflow, use-after-free) yakalar.
   * **TSan (ThreadSanitizer):** Veri yarışlarını (data race) tespit eder.
   * **MSan (MemorySanitizer):** İlklendirilmemiş bellek okumalarını yakalar.
   * **UBSan (UndefinedBehaviorSanitizer):** Tanımsız davranışları (sıfıra bölme, tamsayı taşması) tespit eder.

<div class="callout callout-warning">
<div class="callout-title">Oyun Testlerinde Sanitizer Kullanımı</div>
Oyun geliştirme esnasında Debug derlemelerini ASan ve TSan ile çalıştırmak, karmaşık oyun fiziği ve çok izlekli (multithreaded) iş dizisi (job system) hatalarını aylar öncesinden tespit etmenizi sağlar.
</div>

---

## 5.5 Bölüm Özeti ve İleri Düzey Konular Değerlendirmesi

Bu son bölümde, oyun programlama dili geliştirmede çığır açan ileri düzey LLVM teknolojilerini inceledik. **LLVM ORC JIT (LLJIT)** ve **JITLink** mimarisi sayesinde oyun içi betiklerin yorumlama (interpretation) yavaşlığı olmadan, çalışma zamanında yerel C++ hızında nasıl yürütüldüğünü ve C++ host fonksiyonlarının JIT ortamına nasıl bağlandığını kodladık.

Çok seviyeli ara temsil olan **MLIR (Multi-Level Intermediate Representation)** yapısının matris, vektör ve GPU diyalektleri (Dialects) ile yüksek seviyeli oyun fiziği ve matris optimizasyonlarındaki rolünü ele aldık. CPU ve GPU kod bütünlüğü sağlayan **LLVM SPIR-V** ve **DirectX Shader Compiler (DXC)** araçlarını, derleyici araç zincirini tamamlayan **Clang Tooling**, **LLD** bağlayıcısını ve bellek/izlek hatalarını sıfıra indiren **Clang Sanitizer (ASan, TSan, UBSan)** altyapılarını öğrendik. Bu kapsamlı kılavuz ile LLVM kullanarak sıfırdan yüksek performanslı bir oyun programlama dili inşa etmek için gerekli tüm mimari ve pratik bilgiye sahip oldunuz.
