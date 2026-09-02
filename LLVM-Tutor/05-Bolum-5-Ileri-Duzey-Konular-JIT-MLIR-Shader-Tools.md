# Bölüm 5: İleri Düzey Konular: ORC JIT, MLIR, Shader Kod Üretimi ve Derleyici Araçları

## 5.1 LLVM ORC JIT (On-Request Compilation) Mimarisi ve Oyun İçi Betik (Scripting) Çalıştırma

Oyun motorlarında canlı kod yenileme (Hot-Reloading) ve hızlı betik (scripting) çalıştırma kritik gereksinimlerdir. Geleneksel olarak Lua veya C# gibi diller yorumlanarak (interpreted) veya sanal makine üzerinde çalıştırılır. LLVM **ORC JIT (On-Request Compilation)** API'si sayesinde, oyun içinde yazılan betik kodları çalışma zamanında milisaniyeler içinde doğrudan **öznel makine koduna** derlenir ve sıfır performans kaybı ile çalıştırılır.

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

extern "C" void GameEngine_LogMessage(const char* msg) {
    std::cout << "[Oyun Motoru JIT Log]: " << msg << std::endl;
}

void runJITScript() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    auto jitOrErr = llvm::orc::LLJITBuilder().create();
    if (!jitOrErr) {
        llvm::errs() << "JIT Oluşturulamadı!\n";
        return;
    }
    auto jit = std::move(*jitOrErr);

    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("JITScriptModule", *context);
    llvm::IRBuilder<> builder(*context);

    auto* logFuncType = llvm::FunctionType::get(builder.getVoidTy(), {builder.getPtrTy()}, false);
    auto* logFunc = llvm::Function::Create(logFuncType, llvm::Function::ExternalLinkage, "GameEngine_LogMessage", module.get());

    auto* scriptFuncType = llvm::FunctionType::get(builder.getVoidTy(), false);
    auto* scriptFunc = llvm::Function::Create(scriptFuncType, llvm::Function::ExternalLinkage, "RunScript", module.get());

    auto* bb = llvm::BasicBlock::Create(*context, "entry", scriptFunc);
    builder.SetInsertPoint(bb);

    auto* strVal = builder.CreateGlobalString("LLVM ORC JIT Script Basariyla Calisti!", "script_str");
    builder.CreateCall(logFunc, {strVal});
    builder.CreateRetVoid();

    auto tsm = llvm::orc::ThreadSafeModule(std::move(module), std::move(context));
    cantFail(jit->addIRModule(std::move(tsm)));

    auto symOrErr = jit->lookup("RunScript");
    if (!symOrErr) {
        llvm::errs() << "JIT Sembolü Bulunamadı!\n";
        return;
    }

    void (*scriptFn)() = symOrErr->toPtr<void (*)()>();

    scriptFn();
}
```

---

### Koda Adım Adım Derinlemesine Bakış ve Satır Satır Analiz

Yukarıdaki `jit_engine.cpp` uygulamasında gerçekleşen her bir adımı satır numaralarına referans vererek ayrıntılı olarak inceleyelim:

* **Satır 1 - 8:** Gerekli LLVM başlık dosyaları projeye dahil edilir. `LLJIT.h` JIT motorunun temel sınıfını sağlarken, `TargetSelect.h` hedef mimari sürücülerini başlatır. `raw_ostream.h` LLVM'in özel çıktı akış sistemidir.
* **Satır 10 - 12:** `GameEngine_LogMessage` adlı C++ ev sahibi (host) fonksiyon tanımlanır. Bu fonksiyon, JIT ile derlenen betik kodunun oyun motoru içerisindeki C++ koduna nasıl eriştiğini gösterir. `extern "C"` belirteci, C++ isim karıştırma (name mangling) işlemini engeller ve sembolün C bağlama kuralı ile ORC JIT sembol tablosunda kolayca bulunmasını sağlar.
* **Satır 14 - 16:** `runJITScript` ana fonksiyonu başlatılır. Satır 15 ve 16'da bulunan `llvm::InitializeNativeTarget()` ve `llvm::InitializeNativeTargetAsmPrinter()` çağrıları, derleyicinin üzerinde çalıştığı hedef CPU mimarisini (x86_64, ARM64 vb.) tespit eder ve makine kodu oluşturucuları belleğe yükler.
* **Satır 18 - 23:** `llvm::orc::LLJITBuilder().create()` çağrısı ile bir `LLJIT` örneği oluşturulur. `LLJITBuilder`, ORC JIT mimarisinin en üst seviye kolaylaştırıcı arayüzüdür. Arka planda `ExecutionSession`, `JITDylib` ve `JITLink` yapılarını otomatik olarak yapılandırır. Oluşturma başarısız olursa `jitOrErr` bir LLVM `Error` döndürür ve hata mesajı basılır.
* **Satır 25 - 27:** JIT içinde derlenecek modül için bağımsız bir `LLVMContext` ve `Module` nesnesi oluşturulur. `IRBuilder<>` komut inşa edici başlatılır.
* **Satır 29 - 30:** Ev sahibi C++ fonksiyonunun tür imzası (`void(char*)`) LLVM tarafında `llvm::FunctionType::get` ile tanımlanır ve `GameEngine_LogMessage` adıyla modüle dışsal (External) sembol olarak eklenir.
* **Satır 32 - 36:** JIT betiğinin ana giriş noktası olan `void RunScript()` fonksiyonu ve onun ilk temel bloğu (`entry`) oluşturulur. `IRBuilder` komut ekleme noktası bu bloğa ayarlanır.
* **Satır 38 - 40:** `builder.CreateGlobalString` fonksiyonu ile bellek alanına bir global dizgi (string) sabiti yerleştirilir. Ardından `builder.CreateCall(logFunc, {strVal})` ile C++ ev sahibi fonksiyonu çağrılır ve `builder.CreateRetVoid()` ile fonksiyondan dönülür.
* **Satır 42 - 43:** Oluşturulan `Module` ve `LLVMContext`, thread-safe bir kılıf olan `llvm::orc::ThreadSafeModule` içine aktarılır. `jit->addIRModule` çağrısı ile bu modül JIT derleme kuyruğuna eklenir.
* **Satır 46 - 49:** `jit->lookup("RunScript")` metodu, JIT sembol tablosunda `RunScript` fonksiyonunun yerel RAM bellek adresini arar ve derleme işlemini JITLink üzerinden tetikler.
* **Satır 51:** Bulunan sembolün adresi `toPtr<void (*)()>()` metodu kullanılarak ham bir C++ fonksiyon göstericisine (`function pointer`) dönüştürülür.
* **Satır 53:** `scriptFn()` çağrısı ile RAM'deki yerel makine kodu doğrudan CPU üzerinde sıfır ek maliyetle yürütülür.

---

## 5.2 MLIR (Multi-Level Intermediate Representation) ve Oyun Motorları İçin Önemi

LLVM IR harika bir alt seviye temsil olsa da, yüksek seviyeli matematiksel yapılar (matris çarpımları, GPU thread blokları, oyun içi fizik simülasyonları) LLVM IR seviyesine indirildiğinde kaybolur.

**MLIR (Multi-Level Intermediate Representation)**, LLVM projesinin çok seviyeli ara temsil altyapısıdır. **Dialect (Diyalekt)** adı verilen modüler yapıları sayesinde:
* **`Affine` Dialect:** Döngü optimizasyonları ve bellek erişim desenleri için.
* **`Vector` / `Linalg` Dialect:** Matrix/Vector işlemleri ve SIMD vektörizasyonu için.
* **`GPU` Dialect:** CUDA, ROCm ve Vulkan Compute Kernel yönetimi için.
* **`LLVM` Dialect:** MLIR yapılarından standart LLVM IR'a geçiş (lowering) için.

Oyun dilinizde önce MLIR seviyesinde matris ve vektör optimizasyonları yapabilir, ardından kodu LLVM IR'a indirgeyerek (lowering) donanıma gönderebilirsiniz.

### MLIR Diyalekt İndirgeme (Lowering) Akışı

```
  +-------------------------------------------------------+
  |    Oyun Dili AST (Matris & Fizik Vektör Kodları)      |
  +-------------------------------------------------------+
                              |
                              v
  +-------------------------------------------------------+
  |    High-Level Dialect: Linalg / Vector / Affine       |
  +-------------------------------------------------------+
                              | (High-Level Optimizations)
                              v
  +-------------------------------------------------------+
  |    MLIR LLVM Dialect                                  |
  +-------------------------------------------------------+
                              | (MLIR to LLVM IR Translation)
                              v
  +-------------------------------------------------------+
  |    Standard LLVM IR -> Native Machine Code / SPIR-V   |
  +-------------------------------------------------------+
```

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
