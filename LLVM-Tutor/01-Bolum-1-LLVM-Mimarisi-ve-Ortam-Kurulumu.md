# Bölüm 1: LLVM Mimarisine Giriş, Derleyici Mimarileri ve Oyun Motoru Dilleri İçin LLVM Ortam Kurulumu

## 1.1 Derleyici Mimarilerine Genel Bakış: Klasik vs. Modern Üç Aşamalı (Three-Phase) Yapı

Bir programlama dilinin kaynak kodunu (source code) hedef makinenin anlayabileceği makine koduna (machine code) dönüştürme süreci, bilgisayar bilimlerinin en karmaşık ve zarif alanlarından biridir. Geleneksel (monolitik) derleyicilerde, kaynak dilden hedef mimariye doğrudan bir geçiş yapılırdı. Bu durum, N adet programlama dili ve M adet hedef mimari (x86, ARM, RISC-V, WebAssembly vb.) olduğunda $N \times M$ adet farklı derleyici yazılmasını gerektiriyordu.

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

Bu üç aşamalı yaklaşım sayesinde N dil ve M mimari için ihtiyaç duyulan dönüşüm karmaşıklığı $N + M$ seviyesine inmiştir. LLVM, tam olarak bu mimarinin kalbinde yer alır.

<div class="callout callout-info">
<div class="callout-title">Derleyici Mimarisinde Katman Ayrımı</div>
Frontend dilden anlar fakat işlemciden anlamaz; Backend işlemciden anlar fakat dilden anlamaz; Middle-end ise ne dilden ne de işlemciden anlar, sadece evrensel LLVM IR üzerindeki matematiksel ve mantıksal optimizasyonlara odaklanır.
</div>

---

## 1.2 LLVM Felsefesi ve Ekosistemi

LLVM (kökensel olarak *Low Level Virtual Machine*, ancak günümüzde sadece bir marka isimdir), Chris Lattner ve Vikram Adve tarafından Illinois Üniversitesi'nde başlatılan açık kaynaklı bir derleyici altyapısı projesidir. LLVM'in temel felsefesi **Modülerlik**, **Yeniden Kullanılabilirlik** ve **Dil/Mimari Bağımsızlığıdır**.

LLVM Ekosistemi tek bir derleyiciden ibaret değildir; birbiriyle entegre çalışan devasa bir C++ kütüphaneleri ve araçlar koleksiyonudur:

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

## 1.5 Derleyici Çekirdeği ve Çekirdek C++ Nesneleri: `LLVMContext`, `Module`, ve `IRBuilder` Adım Adım C++ Kodlaması

LLVM C++ API'sinde kod yazarken en çok karşılaşacağınız üç temel C++ yapısı bulunmaktadır. Bu yapılar sadece birer kütüphane nesnesi değil, derleyicinizin bellek ve yürütme mimarisinin omurgasını oluşturur. Bu bölümde her bir nesnenin C++ tarafında adım adım nasıl oluşturulduğunu, değişkenlerin ne işe yaradığını ve arka planda kütüphanenin nasıl çalıştığını detaylıca işleyeceğiz.

### 1. `llvm::LLVMContext`: Derleyici Bellek Havuzu ve Tip Deposu
`LLVMContext`, LLVM'in tüm durum bilgilerini (state), tip tablolarını, sabit tanımlarını ve dahili veri yapılarını yöneten ana çekirdektir.

* **Çalışma Algoritması ve Mantığı:** LLVM bellek yönetimi performans odaklıdır. Örneğin `i32` (32-bit tamsayı) veya `float` tipini her ihtiyaç duyulduğunda yeniden `new` ile tahsis etmek yerine `LLVMContext` içerisinde Unification (Tekilleştirme / Flyweight Deseni) yöntemiyle tek bir defa oluşturur. Böylece milyonlarca değişkene sahip bir projede bile bellek kullanımı ve karşılaştırma (`==`) işlemleri pointer karşılaştırması hızına iner.
* **Benzetme:** `LLVMContext` nesnesini bir fabrikanın **Merkezi Deposu veya Hammadde Omurgası** olarak düşünebilirsiniz. Fabrikada üretilen her ürün (talimatlar, tipler) bu depodaki hammaddeleri ortaklaşa kullanır.
* **İzlek Güvenliği (Thread Safety):** `LLVMContext` tek bir thread üzerinde çalışacak şekilde tasarlanmıştır. Çok izlekli (multithreaded) derleyicilerde (örneğin aynı anda 8 oyun betiğini paralel derlerken), her thread'in kendisine ait ayrı bir `LLVMContext` nesnesi olmalıdır.

#### C++ Tarafında Adım Adım Oluşturulması:
```cpp
// 1. Adım: Standart std::unique_ptr ile bellek güvenliğini sağlayarak bir LLVMContext objesi tahsis ediyoruz.
// Bu 'context' objesi derleyici çalıştığı sürece tüm tiplerin ve sabitlerin tutulduğu merkezi veri deposu olacaktır.
std::unique_ptr<llvm::LLVMContext> context = std::make_unique<llvm::LLVMContext>();
```

### 2. `llvm::Module`: Derleme Birimi Konteyneri
`llvm::Module`, bir kaynak kod dosyasının (Translation Unit) LLVM dünyasındaki karşılığıdır.

* **İçerik ve Yapı:** Bir `Module` içerisinde fonksiyonlar (`llvm::Function`), küresel değişkenler (`llvm::GlobalVariable`), tip tanımları (`llvm::StructType`), veri düzeni (`llvm::DataLayout`) ve hedef donanım bilgisi (`Target Triple`) barındırır.
* **Benzetme:** `Module` yapısını bir oyun projesindeki **Tek Bir C++ Dosyası (`.cpp`) veya C# Class Dosyası** gibi düşünebilirsiniz. Tüm fonksiyonlar bu kutunun içerisinde ikamet eder.

#### C++ Tarafında Adım Adım Oluşturulması ve Bağlanması:
```cpp
// 2. Adım: 'module' isimli derleme birimi konteyner objemizi oluşturuyoruz.
// İlk parametre modülün adıdır ("GameEngineMathModule").
// İkinci parametre ise modülün bağımlı olacağı bellek deposudur (*context).
// Bu bağlantı sayesinde modül içindeki tüm fonksiyon ve değişken tipleri bu context üzerinden yönetilecektir.
std::unique_ptr<llvm::Module> module = std::make_unique<llvm::Module>("GameEngineMathModule", *context);
```

### 3. `llvm::IRBuilder<>`: Kod Jeneratörü ve İmleç Yönetimi
`llvm::IRBuilder<>`, LLVM IR talimatlarını tür güvenli (type-safe) bir biçimde oluşturan ve bunları aktif Basic Block içerisine sırayla yazan yardımcı bir şablon sınıftır.

* **Çalışma Algoritması:** Builder nesnesi dahili bir **Ekleme Noktası İmleci (Insertion Point Cursor)** tutar. Siz `CreateFAdd` veya `CreateRet` çağırdığınızda, builder ilgili C++ talimat nesnesini imlecin gösterdiği Basic Block'un sonuna ekler ve imleci bir adım kaydırır.
* **Benzetme:** `IRBuilder` nesnesi bir kelime işlemci programındaki **Yazı Yazma İmleci (Text Cursor)** gibidir. İmleç neredeyse yeni yazılan metin (LLVM IR talimatı) tam oraya eklenir.

#### C++ Tarafında Adım Adım Oluşturulması ve Konumlandırılması:
```cpp
// 3. Adım: IRBuilder sınıfından 'builder' adında bir kod üretici obje tanımlıyoruz.
// Yapıcı metoda (constructor) adres/referans olarak '*context' veriyoruz.
// 'builder' objesi, yazacağı IR komutlarının tiplerini ve hafıza yerleşimlerini doğrudan bu depodan çekerek uretecektir.
llvm::IRBuilder<> builder(*context);
```

---

### Detaylı C++ Kod Anlatımı: Oyun Vektör Toplama Fonksiyonunun Adım Adım İnşası (`src/main.cpp`)

Aşağıdaki C++ kodunda, bir oyun motorunun matematik kütüphanesinde yer alacak 2B Vektör Toplama fonksiyonunun (`Vec2Add`) C++ LLVM API'si kullanılarak aşama aşama nasıl inşa edildiğini inceleyeceğiz.

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
    // -------------------------------------------------------------------------
    // ADIM 1: Merkezi Depo (LLVMContext) Objesinin Oluşturulması
    // -------------------------------------------------------------------------
    // İlk olarak 'context' isimli akıllı göstericiyi (unique_ptr) tanımlıyoruz.
    // Bu obje, derleme sürecindeki tüm tip bilgilerini, dizgileri ve sabit değerleri
    // dahili tablosunda saklayan ana hafıza havuzudur.
    auto context = std::make_unique<llvm::LLVMContext>();

    // -------------------------------------------------------------------------
    // ADIM 2: Derleme Birimi (Module) Objesinin Tanımlanması ve Bağlanması
    // -------------------------------------------------------------------------
    // 'module' değişkenimiz, kaynak kod dosyamızın (örn. MathUtils.game) LLVM IR
    // karşılığı olan konteynerdir. 'context' referansını alarak onun hafıza
    // havuzuna doğrudan bağlanır.
    auto module = std::make_unique<llvm::Module>("GameEngineMathModule", *context);

    // -------------------------------------------------------------------------
    // ADIM 3: Kod Yazıcı (IRBuilder) Objesinin Başlatılması
    // -------------------------------------------------------------------------
    // 'builder' adında bir IRBuilder objesi türetiyoruz. Bu obje, komutları tek tek
    // oluşturup aktif temel bloğun (BasicBlock) içine imleç yardımıyla dizecektir.
    llvm::IRBuilder<> builder(*context);

    // -------------------------------------------------------------------------
    // ADIM 4: Fonksiyon İmzası ve Tip Tanımlamalarının Yapılması
    // -------------------------------------------------------------------------
    // Oyun dilimizde üretmek istediğimiz fonksiyon: float Vec2Add(float x1, float y1, float x2, float y2)

    // a) 'builder' üzerinden float veri tipini temsil eden 'floatType' pointer'ını çekiyoruz.
    llvm::Type* floatType = builder.getFloatTy();

    // b) Fonksiyonun alacağı parametre tiplerini bir std::vector içinde topluyoruz.
    // Dört adet kayan noktalı sayı (x1, y1, x2, y2) geçilecektir.
    std::vector<llvm::Type*> paramTypes = {floatType, floatType, floatType, floatType};

    // c) 'llvm::FunctionType::get' statik metodunu çağırarak fonksiyon tip şablonunu oluşturuyoruz.
    // İlk parametre dönüş tipi (floatType), ikinci parametre girdi tipleri vektörü (paramTypes),
    // üçüncü parametre ise variadic (değişken sayıda argüman) olup olmadığıdır (false).
    llvm::FunctionType* funcType = llvm::FunctionType::get(floatType, paramTypes, false);

    // d) 'llvm::Function::Create' metodu ile fonksiyon objemizi ('vec2AddFunc') oluşturuyoruz.
    // Parametreler:
    // 1. funcType: Fonksiyonun imzası
    // 2. ExternalLinkage: Fonksiyonun dışarıdan (C/C++ tarafından) çağrılabileceğini belirtir.
    // 3. "Vec2Add": Fonksiyonun sembol ismi.
    // 4. module.get(): Fonksiyonun ekleneceği modül pointer'ı.
    llvm::Function* vec2AddFunc = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        "Vec2Add",
        module.get()
    );

    // e) Fonksiyonun argüman isimlerini belirlemek için parametre iteratörünü başlatıyoruz.
    // 'arg_begin()' ile başlayan iteratör üzerinde gezinerek her parametre objesine ad atıyoruz.
    auto argIt = vec2AddFunc->arg_begin();
    llvm::Value* x1 = argIt++; x1->setName("x1");
    llvm::Value* y1 = argIt++; y1->setName("y1");
    llvm::Value* x2 = argIt++; x2->setName("x2");
    llvm::Value* y2 = argIt++; y2->setName("y2");

    // -------------------------------------------------------------------------
    // ADIM 5: Temel Yapı Taşı (Basic Block) Oluşturma ve İmleç Bağlama
    // -------------------------------------------------------------------------
    // a) 'entryBB' isimli ilk temel bloğumuzu oluşturuyoruz.
    // Bu blok 'vec2AddFunc' fonksiyonunun giriş noktası olacaktır.
    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(*context, "entry", vec2AddFunc);

    // b) 'builder' kod yazıcı objemizin imlecini bu temel bloğun içine konumluyoruz (`SetInsertPoint`).
    // Artık 'builder' ile çağıracağımız tüm 'Create...' metodları komutları bu bloğa ekleyecektir.
    builder.SetInsertPoint(entryBB);

    // -------------------------------------------------------------------------
    // ADIM 6: Matematiksel Hesaplama Talimatlarının Üretilmesi
    // -------------------------------------------------------------------------
    // Toplama işlemleri: x_sum = x1 + x2; y_sum = y1 + y2; total_sum = x_sum + y_sum

    // a) 'builder.CreateFAdd' metodu iki float değerini toplayan LLVM IR 'fadd' komutunu üretir.
    // Dönen 'xSum' objesi bir LLVM IR Sanal Yazmaca (Virtual Register) karşılık gelir.
    llvm::Value* xSum = builder.CreateFAdd(x1, x2, "x_sum");

    // b) Y bileşenlerinin toplamını üretiyoruz.
    llvm::Value* ySum = builder.CreateFAdd(y1, y2, "y_sum");

    // c) İki toplamı birleştirip nihai sonucu üretiyoruz.
    llvm::Value* totalSum = builder.CreateFAdd(xSum, ySum, "total_sum");

    // d) 'builder.CreateRet' metodu ile fonksiyonun sonlandırıcı talimatını (Terminator Instruction) yazıyoruz.
    // 'totalSum' değerini çağıran tarafa döndürür.
    builder.CreateRet(totalSum);

    // -------------------------------------------------------------------------
    // ADIM 7: Üretilen Kodun Yapısal Doğrulaması (Validation)
    // -------------------------------------------------------------------------
    // 'llvm::verifyFunction' fonksiyonu oluşturduğumuz 'vec2AddFunc' C++ objesini tarayarak
    // tip uyuşmazlığı veya eksik sonlandırıcı gibi bir kural ihlali olup olmadığını denetler.
    if (llvm::verifyFunction(*vec2AddFunc, &llvm::outs())) {
        llvm::errs() << "HATA: Vec2Add fonksiyonu LLVM IR kurallarına uymuyor!\n";
        return 1;
    }

    // -------------------------------------------------------------------------
    // ADIM 8: Üretilen Metinsel LLVM IR Kodunun Ekrana Yazdırılması
    // -------------------------------------------------------------------------
    llvm::outs() << "=== Oyun Dili Vec2Add LLVM IR Ciktisi ===\n";
    module->print(llvm::outs(), nullptr);

    return 0;
}
```

<div class="callout callout-tip">
<div class="callout-title">İpucu: LLVM IR Doğrulama (llvm::verifyFunction)</div>
`llvm::verifyFunction` çağrısı, oluşturduğunuz IR'ın LLVM tip sistemine ve SSA kurallarına uyup uymadığını denetler. Derleyici geliştirirken bu adımı atlamamak, beklenmeyen çalışma zamanı çökmelerini önler.
</div>

---

### Koda Adım Adım Derinlemesine Bakış ve Yürütme Algoritması

1. **`auto context = std::make_unique<llvm::LLVMContext>();`**
   * *Çalışma Mantığı:* LLVM evreninin ilk adımıdır. Tüm veri tipleri (float, i32, pointer vb.) bu context nesnesinin bellek tablosunda tutulur. Derleyicimiz sonlandığında `unique_ptr` sayesinde bellek sızıntısı olmadan otomatik temizlenir.
2. **`auto module = std::make_unique<llvm::Module>("GameEngineMathModule", *context);`**
   * *Çalışma Mantığı:* `GameEngineMathModule` isimli yeni bir derleme birimi konteyneri açar. Üretilecek `Vec2Add` fonksiyonu bu modülün sembol tablosuna kaydedilecektir.
3. **`llvm::FunctionType::get(floatType, paramTypes, false);`**
   * *Çalışma Mantığı:* C++'taki `float(float, float, float, float)` fonksiyon imzasını oluşturur. Üçüncü parametre olan `false`, fonksiyonun variadic (değişken sayıda parametre alan `printf` gibi) olmadığını belirtir.
4. **`llvm::Function::Create(...)`**
   * *Çalışma Mantığı:* Fonksiyon nesnesini hafızada tahsis eder. `ExternalLinkage` parametresi, bu fonksiyonun C++ veya diğer dış kütüphaneler tarafından çağrılabileceğini (public/global olduğunu) ifade eder.
5. **`vec2AddFunc->arg_begin()` İteratörü**
   * *Çalışma Mantığı:* Fonksiyonun parametre listesi üzerinde sırayla geçerek C++ nesnelerine (`x1`, `y1`, `x2`, `y2`) isim atar. Bu isimler metin formatındaki `.ll` çıktısında değişken adı olarak görünür.
6. **`BasicBlock::Create(*context, "entry", vec2AddFunc)` ve `builder.SetInsertPoint(entryBB)`**
   * *Çalışma Mantığı:* Fonksiyonun ilk kod bloğunu (`entry`) açar. `SetInsertPoint` çağrısı `IRBuilder` imlecini bu bloğun içine konumlandırır. Artık builder ile yazılacak tüm komutlar bu bloğa eklenecektir.
7. **`builder.CreateFAdd(...)`**
   * *Çalışma Mantığı:* Kayan noktalı toplama (`fadd`) talimatlarını üretir. İlk çağrıda `x1` ve `x2` toplanıp sanal yazmaca yazılır, ikinci çağrıda `y1` ve `y2` toplanır, üçüncü çağrıda ise iki toplam birleştirilir.
8. **`llvm::verifyFunction(*vec2AddFunc, ...)`**
   * *Çalışma Mantığı:* Yapısal doğrulama algoritması çalıştırılır. Fonksiyonun bir sonlandırıcı (`ret`) ile bitip bitmediği, tiplerin uyuşup uyuşmadığı ve SSA kurallarının ihlal edilip edilmediği denetlenir.

---

## 1.6 Bölüm Özeti ve Derleyici Mimarisi Değerlendirmesi

Bu ilk bölümde, modern derleyici tasarımının temel taşı olan **Üç Aşamalı Mimariyi (Three-Phase Architecture)** ve bu mimarinin oyun programlama dili geliştirmedeki stratejik rolünü inceledik. $N \times M$ karmaşıklığını $N + M$ seviyesine indiren bu yapı, dilden ve donanımdan bağımsız evrensel bir ara temsil (LLVM IR) üzerinden çalışmaktadır.

LLVM ekosisteminin sunduğu modüler C++ kütüphaneleri, Clang ön yüzü, LLD bağlayıcısı ve ORC JIT altyapısı sayesinde oyun motorlarında yüksek performans, canlı betik derleme (Hot-Reloading) ve verimli bellek yönetimi elde edilebilmektedir. C++ tarafında `LLVMContext` bellek deposu, `Module` derleme konteyneri ve `IRBuilder` kod üreticisi nesneleri ile ilk fonksiyonumuz olan `Vec2Add` modülünü hafızada adım adım inşa edip doğruladık. Bir sonraki bölümde, dilimizin Ön Yüzünü (Frontend) inşa etmek üzere Soyut Sözdizim Ağaçları (AST), SSA formu ve LLVM bellek modelinin C++ ile adım adım uygulanmasını ele alacağız.
