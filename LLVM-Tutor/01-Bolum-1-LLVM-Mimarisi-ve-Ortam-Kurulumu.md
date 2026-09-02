# Bölüm 1: LLVM Mimarisine Giriş, Derleyici Mimarileri ve Oyun Motoru Dilleri İçin LLVM Ortam Kurulumu

## 1.1 Derleyici Mimarilerine Genel Bakış: Klasik vs. Modern Üç Aşamalı (Three-Phase) Yapı

Bir programlama dilinin kaynak kodunu (source code) hedef makinenin anlayabileceği makine koduna (machine code) dönüştürme süreci, bilgisayar bilimlerinin en karmaşık ve zarif alanlarından biridir. Geleneksel (monolitik) derleyicilerde, kaynak dilden hedef mimariye doğrudan bir geçiş yapılırdı. Bu durum, N adet programlama dili ve M adet hedef mimari (x86, ARM, RISC-V, WebAssembly vb.) olduğunda N × M adet farklı derleyici yazılmasını gerektiriyordu.

Modern derleyici tasarımında bu problem, **Üç Aşamalı Mimariler (Three-Phase Compiler Architecture)** ile çözülmüştür. Bu mimari üç ana bileşenden oluşur:

1. **Frontend (Ön Yüz):** Kaynak kodu okur, sözdizimsel (lexical) ve dilbilgisel (syntactic) analizini yapar, Tip Denetimi (Type Checking) gerçekleştirir ve dili temsil eden bir **Soyut Sözdizim Ağacı (Abstract Syntax Tree - AST)** oluşturur. Son olarak bu AST'yi dile bağımsız bir **Ara Temsile (Intermediate Representation - IR)** dönüştürür.
2. **Middle-end (Orta Yüz / Optimizasyon Katmanı):** Üretilen IR üzerinde çalışır. Kaynak dilden ve hedef donanımdan bağımsız olarak kod üzerindeki gereksiz hesaplamaları ayıklar, döngüleri optimize eder, bellek erişimlerini iyileştirir ve kodu en verimli hale getirir.
3. **Backend (Arka Yüz / Kod Üretimi):** Optimize edilmiş IR'ı alır, hedef mimarinin yazmaç (register) kısıtlarına, buyruk kümesine (instruction set) ve yürütme boru hattına (pipeline) göre makine koduna (assembly / makine komutları) dönüştürür.

```
+-----------------------------------------------------------------------+
|                            FRONTEND                                   |
| Kaynak Kod (.game) -> Lexer -> Parser -> AST -> Type Checker -> IR    |
+-----------------------------------------------------------------------+
                                   |
                                   v
+-----------------------------------------------------------------------+
|                            MIDDLE-END                                 |
|   LLVM IR Pass Manager -> Mem2Reg -> CSE -> DCE -> Loop Unroll        |
+-----------------------------------------------------------------------+
                                   |
                                   v
+-----------------------------------------------------------------------+
|                            BACKEND                                    |
| LLVM IR -> Instruction Selection -> Register Allocation -> Assembly   |
+-----------------------------------------------------------------------+
```

Bu üç aşamalı yaklaşım sayesinde N dil ve M mimari için ihtiyaç duyulan dönüşüm karmaşıklığı N + M seviyesine inmiştir. LLVM, tam olarak bu mimarinin kalbinde yer alır.

<div class="callout callout-info">
<div class="callout-title">Derleyici Mimarisinde Katman Ayrımı</div>
Frontend dilden anlar fakat işlemciden anlamaz; Backend işlemciden anlar fakat dilden anlamaz; Middle-end ise ne dilden ne de işlemciden anlar, sadece evrensel LLVM IR üzerindeki matematiksel ve mantıksal optimizasyonlara odaklanır.
</div>

---

## 1.2 LLVM Felsefesi ve Ekosistemi

LLVM (kökensel olarak *Low Level Virtual Machine*, ancak günümüzde sadece bir marka isimdir), Chris Lattner ve Vikram Adve tarafından Illinois Üniversitesi'nde başlatılan açık kaynaklı bir derleyici altyapısı projesidir. LLVM'in temel felsefesi **Modülerlik**, **Yeniden Kullanılabilirlik** ve **Dil/Mimari Bağımsızlığıdır**.

LLVM Ekosistemi tek bir derleyiciden ibaret değildir; birbiriyle entegre çalışan devasa bir araçlar ve kütüphaneler koleksiyonudur:

* **LLVM Core:** Temel kod optimizasyonu ve her türlü işlemci mimarisi için kod üretimi sağlayan C++ kütüphaneleri koleksiyonu.
* **Clang:** C, C++, Objective-C ve OpenCL için geliştirilmiş, LLVM tabanlı ultra hızlı bir Frontend derleyicisi.
* **LLD:** LLVM projesinin geliştirdiği, GNU gold veya MSVC link.exe'ye kıyasla katlarca hızlı çalışan sistem bağlayıcısı (Linker).
* **LLDB:** LLVM mimarisi üzerine kurulu yüksek performanslı hata ayıklayıcı (Debugger).
* **MLIR (Multi-Level Intermediate Representation):** Etki alanına özel (Domain-Specific) derleyiciler ve yapay zeka/makine öğrenmesi modelleri için çok seviyeli ara temsil altyapısı.
* **LLVM ORC JIT:** Çalışma zamanında (JIT - Just-In-Time) dinamik kod derlemeyi sağlayan modüler C++ API'si.

---

## 1.3 Oyun Motorları İçin Özel Programlama Dili Geliştirme İhtiyacı

Oyun geliştirme; yüksek performans (FPS kısıtları), düşük gecikme (latency), esnek betik yazımı (scripting) ve GPU/CPU arasında kesintisiz haberleşme gerektiren zorlu bir alandır. C++ geleneksel olarak sektör standardı olsa da, derleme sürelerinin uzunluğu ve esnek olmayan yapısı nedeniyle oyun projelerinde sıklıkla betik dilleri (Lua, C#, Cscript vb.) kullanılır.

Ancak Lua gibi yorumlanan (interpreted) diller çalışma zamanında ciddi performans kayıplarına yol açar. Oyun motorları için özel bir dil oluştururken LLVM kullanmanın kritik avantajları şunlardır:

1. **Sıfır Maliyetli Soyutlama (Zero-Cost Abstraction):** LLVM'in güçlü optimizasyon katmanı sayesinde dilde yazdığınız yüksek seviyeli yapılar (örneğin 3D Vektör toplama, ECS bileşen erişimleri) doğrudan inline C++ seviyesinde makine koduna dönüşür.
2. **JIT Scripting Hibrit Yapısı:** Oyun oynanırken betik kodlarını çalışma zamanında LLVM ORC JIT ile makine koduna derleyebilir, interpret etme yavaşlığından tamamen kurtulabilirsiniz.
3. **GPU Shader ve CPU Kod Bütünlüğü:** LLVM SPIR-V veya DirectX Shader Compiler (DXC) altyapısı sayesinde oyun dilinizden doğrudan Vulkan/DirectX shader kodları üretebilirsiniz.
4. **Veri Odaklı Tasarım (Data-Oriented Design) Desteği:** Bellek düzenini (SIMD alignment, Struct-of-Arrays) LLVM IR seviyesinde milimetrik olarak kontrol edebilirsiniz.

---

## 1.4 Oyun Motoru C++ Geliştirme Ortamı ve LLVM Kurulumu

Oyun dilinizi yazarken LLVM C++ kütüphanelerini projenize bağlamanız gerekir. Modern C++ (C++17 / C++20) standartları ile LLVM API'lerini kullanacağız.

### CMake Entegrasyonu (`CMakeLists.txt`)

Aşağıda, sisteminizde kurulu olan LLVM paketini otomatik tespit edip C++ projenize bağlayan profesyonel bir `CMakeLists.txt` konfigürasyonu yer almaktadır:

```cmake
cmake_minimum_required(VERSION 3.20)
project(GameLangCompiler LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# LLVM paketini sistemde ara
find_package(LLVM REQUIRED CONFIG)

message(STATUS "LLVM Bulundu: sürüm ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM Dahil Etme Dizini: ${LLVM_INCLUDE_DIRS}")
message(STATUS "LLVM Tanımlamaları: ${LLVM_DEFINITIONS}")

# LLVM Başlık Dosyalarını Ekle
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Oyun Derleyicisi Çalıştırılabilir Dosyası
add_executable(gamelang_compiler
    src/main.cpp
)

# Gerekli LLVM Bileşenlerini/Kütüphanelerini Map'le
llvm_map_components_to_libnames(llvm_libs
    Core
    Analysis
    ExecutionEngine
    InstCombine
    Object
    OrcJIT
    ScalarOpts
    Support
    TransformUtils
    native
)

# Kütüphaneleri Hedefe Bağla
target_link_libraries(gamelang_compiler PRIVATE ${llvm_libs})
```

---

## 1.5 İlk LLVM C++ Programı: `LLVMContext`, `Module`, ve `IRBuilder` Kavramları

LLVM C++ API'sinde çalışırken bilmeniz gereken üç temel yapı vardır:

1. **`llvm::LLVMContext`:** LLVM'in tüm durum bilgilerini (tip tabloları, sabitler, çekirdek veri yapıları) yöneten ipucu nesnesidir. İki farklı thread üzerinde çalışırken her thread kendi `LLVMContext` nesnesine sahip olmalıdır.
2. **`llvm::Module`:** Bir derleme birimini (Translation Unit) temsil eden ana konteynerdir. İçerisinde fonksiyonlar, küresel değişkenler, tip tanımları ve hedef mimari bilgileri barındırır.
3. **`llvm::IRBuilder<>`:** Basic Block'lar içerisine adım adım LLVM IR talimatı (instruction) ekleyen, tür güvenli yardımcı C++ şablon sınıfıdır.

### Detaylı C++ Örneği: Oyun Vektör Toplama Fonksiyonunun LLVM IR Olarak Üretilmesi (`src/main.cpp`)

Aşağıdaki C++ kodu, oyun motorlarında sıkça kullanılan iki boyutlu bir vektör toplama fonksiyonunu (`Vec2Add`) LLVM C++ API'sini kullanarak hafızada oluşturur:

```cpp
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <iostream>
#include <memory>
#include <vector>

int main() {
    // 1. LLVM Context Oluşturma
    auto context = std::make_unique<llvm::LLVMContext>();

    // 2. Oyun Dili Modülü Oluşturma
    auto module = std::make_unique<llvm::Module>("GameEngineMathModule", *context);

    // 3. IR Builder Nesnesi
    llvm::IRBuilder<> builder(*context);

    // 4. Vector2D Toplama Fonksiyonu İmzası Oluşturma: float Vec2Add(float x1, float y1, float x2, float y2)
    // Parametre tipleri: [float, float, float, float]
    llvm::Type* floatType = builder.getFloatTy();
    std::vector<llvm::Type*> paramTypes = {floatType, floatType, floatType, floatType};

    // Fonksiyon Tipi: Geri dönüş float, parametreler float vector bileşenleri
    llvm::FunctionType* funcType = llvm::FunctionType::get(floatType, paramTypes, false);

    // Fonksiyonu Modüle Ekleme (External Linkage)
    llvm::Function* vec2AddFunc = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        "Vec2Add",
        module.get()
    );

    // Parametre İsimlerini Belirleme
    auto argIt = vec2AddFunc->arg_begin();
    llvm::Value* x1 = argIt++; x1->setName("x1");
    llvm::Value* y1 = argIt++; y1->setName("y1");
    llvm::Value* x2 = argIt++; x2->setName("x2");
    llvm::Value* y2 = argIt++; y2->setName("y2");

    // 5. Basic Block (Giriş Bloğu) Oluşturma
    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(*context, "entry", vec2AddFunc);
    builder.SetInsertPoint(entryBB);

    // 6. İşlemler: x_sum = x1 + x2; y_sum = y1 + y2; result = x_sum + y_sum
    llvm::Value* xSum = builder.CreateFAdd(x1, x2, "x_sum");
    llvm::Value* ySum = builder.CreateFAdd(y1, y2, "y_sum");
    llvm::Value* totalSum = builder.CreateFAdd(xSum, ySum, "total_sum");

    // Fonksiyondan Dönen Değer
    builder.CreateRet(totalSum);

    // 7. Oluşturulan Fonksiyonun Doğruluğunu (Validation) Kontrol Etme
    std::string errorStr;
    llvm::raw_string_error_stream errorStream(errorStr);
    if (llvm::verifyFunction(*vec2AddFunc, &llvm::outs())) {
        llvm::errs() << "HATA: Vec2Add fonksiyonu LLVM IR kurallarına uymuyor!\n";
        return 1;
    }

    // 8. Modülün IR Çıktısını Ekrana Yazdırma
    llvm::outs() << "=== Oyun Dili Vec2Add LLVM IR Ciktisi ===\n";
    module.print(llvm::outs(), nullptr);

    return 0;
}
```

<div class="callout callout-tip">
<div class="callout-title">İpucu: LLVM IR Doğrulama (llvm::verifyFunction)</div>
`llvm::verifyFunction` çağrısı, oluşturduğunuz IR'ın LLVM tip sistemine ve SSA kurallarına uyup uymadığını denetler. Derleyici geliştirirken bu adımı atlamamak, beklenmeyen çalışma zamanı çökmelerini önler.
</div>

### Koda Adım Adım Bakış ve Açıklaması:

1. **`getFloatTy()`**: LLVM Context üzerinden tek duyarlıklı kayan noktalı sayı (`float`) tipini alır.
2. **`FunctionType::get`**: Fonksiyonun geri dönüş tipini ve alacağı parametre listesini tanımlayan metatipi üretir.
3. **`Function::Create`**: Oluşturulan fonksiyonu `GameEngineMathModule` içerisine yerleştirir.
4. **`BasicBlock::Create`**: Fonksiyonun kod yürütme adımlarının tutulacağı ilk Temel Bloğu (`entry`) tanımlar.
5. **`builder.CreateFAdd`**: Kayan noktalı sayılar için LLVM `fadd` talimatını üretir.
