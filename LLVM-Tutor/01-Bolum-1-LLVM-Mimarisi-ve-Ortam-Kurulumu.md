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

find_package(LLVM REQUIRED CONFIG)

message(STATUS "LLVM Bulundu: sürüm ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM Dahil Etme Dizini: ${LLVM_INCLUDE_DIRS}")
message(STATUS "LLVM Tanımlamaları: ${LLVM_DEFINITIONS}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_executable(gamelang_compiler
    src/main.cpp
)

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

target_link_libraries(gamelang_compiler PRIVATE ${llvm_libs})
```

---

## 1.5 Derleyici Çekirdeği ve Çekirdek C++ Nesneleri: `LLVMContext`, `Module`, ve `IRBuilder` Adım Adım C++ Kodlaması

LLVM C++ API'sinde kod yazarken en çok karşılaşacağınız üç temel C++ yapısı bulunmaktadır. Bu yapılar sadece birer kütüphane nesnesi değil, derleyicinizin bellek ve yürütme mimarisinin omurgasını oluşturur. Bu bölümde her bir nesnenin C++ tarafında nasıl oluşturulduğunu ve çalışma mantığını adım adım inceleyeceğiz.

### 1. `llvm::LLVMContext`: Derleyici Bellek Havuzu ve Tip Deposu
`LLVMContext`, LLVM'in tüm durum bilgilerini (state), tip tablolarını, sabit tanımlarını ve dahili veri yapılarını yöneten ana çekirdektir.

* **Çalışma Algoritması ve Mantığı:** LLVM bellek yönetimi performans odaklıdır. Örneğin `i32` (32-bit tamsayı) veya `float` tipini her ihtiyaç duyulduğunda yeniden `new` ile tahsis etmek yerine `LLVMContext` içerisinde Unification (Tekilleştirme / Flyweight Deseni) yöntemiyle tek bir defa oluşturur. Böylece milyonlarca değişkene sahip bir projede bile bellek kullanımı ve karşılaştırma (`==`) işlemleri pointer karşılaştırması hızına iner.
* **Benzetme:** `LLVMContext` nesnesini bir fabrikanın **Merkezi Deposu veya Hammadde Omurgası** olarak düşünebilirsiniz. Fabrikada üretilen her ürün (talimatlar, tipler) bu depodaki hammaddeleri ortaklaşa kullanır.
* **İzlek Güvenliği (Thread Safety):** `LLVMContext` tek bir thread üzerinde çalışacak şekilde tasarlanmıştır. Çok izlekli (multithreaded) derleyicilerde (örneğin aynı anda 8 oyun betiğini paralel derlerken), her thread'in kendisine ait ayrı bir `LLVMContext` nesnesi olmalıdır.

#### C++ Tarafında Tanımlanması:

```cpp
std::unique_ptr<llvm::LLVMContext> context = std::make_unique<llvm::LLVMContext>();
```

#### Adım Adım Açıklama ve Çalışma İlkesi:

1. **Bellek Tahsisi ve Sahiplik Yönetimi:** Bu satırda Modern C++'ın en güvenli araçlarından biri olan `std::unique_ptr` kullanılmaktadır. `std::make_unique<llvm::LLVMContext>()` ifadesi, heap üzerinde yeni bir `LLVMContext` nesnesi oluşturur ve bu nesnenin sahipliğini `context` akıllı göstericisine verir.
2. **Neden `unique_ptr` Tercih Edilir?:** Derleyici çalışma sürecinde bellek sızıntılarını (memory leak) engellemek kritik önem taşır. `context` nesnesi kapsam dışına (out of scope) çıktığında, yıkıcı metod (destructor) otomatik tetiklenir ve LLVM'in hafızada tuttuğu devasa tip tabloları güvenle temizlenir.
3. **Oluşturulduktan Sonraki Durum:** Nesne oluşturulduğu anda içerisinde temel LLVM veri tipleri (`i1`, `i8`, `i32`, `i64`, `float`, `double`, `void`) önceden hazır tutulur. Derleyicinizin ilerleyen safhalarında üreteceğiniz tüm fonksiyonlar ve değişkenler bu merkezi depoya referans vererek hayatına devam edecektir.

---

### 2. `llvm::Module`: Derleme Birimi Konteyneri
`llvm::Module`, bir kaynak kod dosyasının (Translation Unit) LLVM dünyasındaki karşılığıdır.

* **İçerik ve Yapı:** Bir `Module` içerisinde fonksiyonlar (`llvm::Function`), küresel değişkenler (`llvm::GlobalVariable`), tip tanımları (`llvm::StructType`), veri düzeni (`llvm::DataLayout`) ve hedef donanım bilgisi (`Target Triple`) barındırır.
* **Benzetme:** `Module` yapısını bir oyun projesindeki **Tek Bir C++ Dosyası (`.cpp`) veya C# Class Dosyası** gibi düşünebilirsiniz. Tüm fonksiyonlar bu kutunun içerisinde ikamet eder.

#### C++ Tarafında Tanımlanması:

```cpp
std::unique_ptr<llvm::Module> module = std::make_unique<llvm::Module>("GameEngineMathModule", *context);
```

#### Adım Adım Açıklama ve Çalışma İlkesi:

1. **Modül İsmi Parametresi (`"GameEngineMathModule"`):** `llvm::Module` sınıfının yapıcı metoduna (constructor) verilen ilk parametre metinsel modül adıdır. Bu isim, hata ayıklama (debugging) ve bağlama (linking) aşamalarında ilgili kod bloğunun hangi derleme biriminden geldiğini gösterir.
2. **Context Bağlantısı (`*context`):** İkinci parametre olarak yukarıda oluşturduğumuz `context` nesnesinin dereference edilmiş referansı verilir. Bu bağlantı hayati bir zorunluluktur; çünkü modül içinde yaratılacak tüm fonksiyonlar ve küresel veriler, veri tiplerini bu `context` deposundan çekecektir.
3. **Modülün Dahili Yapısı:** Modül nesnesi oluşturulduğunda sembol tablosu (Symbol Table) boş olarak başlatılır. Modüle yeni fonksiyonlar eklendikçe, modül bu fonksiyonları çift yönlü bağlı liste (doubly-linked list) veri yapısında tutar.

---

### 3. `llvm::IRBuilder<>`: Kod Jeneratörü ve İmleç Yönetimi
`llvm::IRBuilder<>`, LLVM IR talimatlarını tür güvenli (type-safe) bir biçimde oluşturan ve bunları aktif Basic Block içerisine sırayla yazan yardımcı bir şablon sınıfıdır.

* **Çalışma Algoritması:** Builder nesnesi dahili bir **Ekleme Noktası İmleci (Insertion Point Cursor)** tutar. Siz `CreateFAdd` veya `CreateRet` çağırdığınızda, builder ilgili C++ talimat nesnesini imlecin gösterdiği Basic Block'un sonuna ekler ve imleci bir adım kaydırır.
* **Benzetme:** `IRBuilder` nesnesi bir kelime işlemci programındaki **Yazı Yazma İmleci (Text Cursor)** gibidir. İmleç neredeyse yeni yazılan metin (LLVM IR talimatı) tam oraya eklenir.

#### C++ Tarafında Tanımlanması:

```cpp
llvm::IRBuilder<> builder(*context);
```

#### Adım Adım Açıklama ve Çalışma İlkesi:

1. **Şablon Sınıf Yapısı (`IRBuilder<>`):** `IRBuilder` bir şablon (template) sınıf olup varsayılan parametrelerle kullanılır. İstenirse özel bellek tahsis ediciler (allocators) veya işaretçi koruyucular ile özelleştirilebilir.
2. **Context İle Başlatma:** Yapıcı metoda verilen `*context` referansı, builder nesnesine kod üretirken ihtiyaç duyacağı temel sabitleri ve tipleri nereden alacağını söyler.
3. **İlk Durum (Unpositioned Cursor):** `builder` objesi ilk türetildiğinde henüz geçerli bir ekleme noktasına (BasicBlock) sahip değildir. Kod üretmeye başlamadan önce mutlaka bir `builder.SetInsertPoint(basicBlock)` çağrısı yapılması gerekmektedir.

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
    auto context = std::make_unique<llvm::LLVMContext>();

    auto module = std::make_unique<llvm::Module>("GameEngineMathModule", *context);

    llvm::IRBuilder<> builder(*context);

    llvm::Type* floatType = builder.getFloatTy();

    std::vector<llvm::Type*> paramTypes = {floatType, floatType, floatType, floatType};

    llvm::FunctionType* funcType = llvm::FunctionType::get(floatType, paramTypes, false);

    llvm::Function* vec2AddFunc = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        "Vec2Add",
        module.get()
    );

    auto argIt = vec2AddFunc->arg_begin();
    llvm::Value* x1 = argIt++; x1->setName("x1");
    llvm::Value* y1 = argIt++; y1->setName("y1");
    llvm::Value* x2 = argIt++; x2->setName("x2");
    llvm::Value* y2 = argIt++; y2->setName("y2");

    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(*context, "entry", vec2AddFunc);

    builder.SetInsertPoint(entryBB);

    llvm::Value* xSum = builder.CreateFAdd(x1, x2, "x_sum");

    llvm::Value* ySum = builder.CreateFAdd(y1, y2, "y_sum");

    llvm::Value* totalSum = builder.CreateFAdd(xSum, ySum, "total_sum");

    builder.CreateRet(totalSum);

    if (llvm::verifyFunction(*vec2AddFunc, &llvm::outs())) {
        llvm::errs() << "HATA: Vec2Add fonksiyonu LLVM IR kurallarına uymuyor!\n";
        return 1;
    }

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

Yukarıdaki kod bloğunda yer alan C++ ifadelerinin her birinin arka planda ne yaptığını, değişkenlerin işlevlerini ve LLVM'in mantıksal akışını adım adım detaylandıralım:

#### Adım 1: Temel LLVM Obje Kurulumları
* `auto context = std::make_unique<llvm::LLVMContext>();`
  * Bu satırda, derleyicinin hafıza havuzu tahsis edilir. `context` objesi, tüm tiplerin ve sembollerin adreslerini yönetecektir.
* `auto module = std::make_unique<llvm::Module>("GameEngineMathModule", *context);`
  * `GameEngineMathModule` isimli derleme birimi oluşturulur. Bu modül, üreteceğimiz `Vec2Add` fonksiyonunu barındıracak ana kap olacaktır.
* `llvm::IRBuilder<> builder(*context);`
  * IR talimatlarını sırayla üretecek olan kod yazıcı nesnesi başlatılır.

#### Adım 2: Fonksiyon İmzasının ve Tiplerinin Hazırlanması
* `llvm::Type* floatType = builder.getFloatTy();`
  * `builder.getFloatTy()` metodu çağrılarak `context` içerisinden tek hassasiyetli (32-bit IEEE 754) kayan noktalı sayı tipini temsil eden `llvm::Type*` adresi alınır.
* `std::vector<llvm::Type*> paramTypes = {floatType, floatType, floatType, floatType};`
  * Oyun vektörümüz 2 boyutlu iki noktadan oluşmaktadır: $V_1 = (x_1, y_1)$ ve $V_2 = (x_2, y_2)$. Fonksiyona 4 adet `float` parametre geçileceği için 4 elemanlı bir `std::vector` oluşturulur.
* `llvm::FunctionType* funcType = llvm::FunctionType::get(floatType, paramTypes, false);`
  * `llvm::FunctionType::get` statik fabrikasyon metodu çağrılır.
  * **İlk Parametre (`floatType`):** Fonksiyonun dönüş tipidir. Fonksiyon $x$ ve $y$ toplamlarının bileşkesini `float` olarak döndürecektir.
  * **İkinci Parametre (`paramTypes`):** Aldığı 4 adet float parametreyi belirten liste.
  * **Üçüncü Parametre (`false`):** Fonksiyonun variadic (C dillerindeki `printf(const char*, ...)` gibi değişken sayıda argüman alan) olmadığını belirtir.

#### Adım 3: Fonksiyon Nesnesinin (`llvm::Function`) Oluşturulması
* `llvm::Function* vec2AddFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "Vec2Add", module.get());`
  * `llvm::Function::Create` metodu ile fonksiyon bellek alanı modül içinde tahsis edilir.
  * **`funcType`:** Az önce hazırladığımız fonksiyon imzası.
  * **`ExternalLinkage`:** Bu fonksiyonun sembol tablosunda dışarıya açık (public/global) olacağını, yani C++ veya başka bir dış kütüphane tarafından `Vec2Add` ismiyle çağrılabileceğini ifade eder.
  * **`"Vec2Add"`:** Fonksiyonun metinsel sembol adı.
  * **`module.get()`:** Fonksiyonun ekleneceği modülün ham pointer adresi.

#### Adım 4: Argüman İsimlerinin Atanması
* `auto argIt = vec2AddFunc->arg_begin();`
  * `vec2AddFunc->arg_begin()` çağrısı, fonksiyon parametre listesinin başlangıç iteratörünü verir.
* `llvm::Value* x1 = argIt++; x1->setName("x1");` ...
  * İteratör adım adım ilerletilerek her parametre nesnesine (`llvm::Argument`) sırasıyla `"x1"`, `"y1"`, `"x2"`, `"y2"` isimleri atanır. Bu isimler hem metinsel IR (.ll) çıktısında okunabilirliği sağlar hem de SSA sanal yazmaç isimlerinin temelini oluşturur.

#### Adım 5: Temel Yapı Taşının (Basic Block) Açılması ve İmlecin Yerleştirilmesi
* `llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(*context, "entry", vec2AddFunc);`
  * `BasicBlock::Create` metodu ile fonksiyonun ilk kodu olan `"entry"` bloğu oluşturulur. İkinci parametre olan `vec2AddFunc`, bu bloğun doğrudan ilgili fonksiyona bağlanmasını sağlar.
* `builder.SetInsertPoint(entryBB);`
  * `builder` imleci `"entry"` bloğunun başlangıcına yerleştirilir. Bu andan itibaren `builder.Create...` şeklinde çağrılacak tüm komutlar bu bloğun içine sırayla eklenecektir.

#### Adım 6: Matematiksel Hesaplama ve Sonlandırıcı Talimatların Yazılması
* `llvm::Value* xSum = builder.CreateFAdd(x1, x2, "x_sum");`
  * `builder.CreateFAdd` metodu float toplama komutu (`fadd`) üretir. `x1` ve `x2` parametrelerini toplar ve sonucu `"x_sum"` adıyla bir SSA sanal yazmacına atar.
* `llvm::Value* ySum = builder.CreateFAdd(y1, y2, "y_sum");`
  * `y1` ve `y2` parametreleri toplanarak `"y_sum"` sanal yazmacına yazılır.
* `llvm::Value* totalSum = builder.CreateFAdd(xSum, ySum, "total_sum");`
  * Elde edilen iki ara toplam bileşkesi (`xSum` ve `ySum`) tekrar toplanarak `"total_sum"` yazmacına kaydedilir.
* `builder.CreateRet(totalSum);`
  * Bloğun sonlandırıcı talimatı (Terminator Instruction) olan `ret` komutu üretilir ve `totalSum` değeri çağıran tarafa döndürülür.

#### Adım 7: Yapısal Control ve IR Çıktısının Yazdırılması
* `llvm::verifyFunction(*vec2AddFunc, &llvm::outs())`
  * Oluşturulan fonksiyon üzerinde tutarlılık analizi yapılır. Eksik `ret` komutu veya tip uyuşmazlığı varsa hata mesajı basılır.
* `module->print(llvm::outs(), nullptr);`
  * Bellekte oluşturulan LLVM IR veri yapısı, standart çıktı akışına (`llvm::outs()`) metinsel olarak yazdırılır.

---

## 1.6 Bölüm Özeti ve Derleyici Mimarisi Değerlendirmesi

Bu ilk bölümde, modern derleyici tasarımının temel taşı olan **Üç Aşamalı Mimariyi (Three-Phase Architecture)** ve bu mimarinin oyun programlama dili geliştirmedeki stratejik rolünü inceledik. N × M karmaşıklığını N + M seviyesine indiren bu yapı, dilden ve donanımdan bağımsız evrensel bir ara temsil (LLVM IR) üzerinden çalışmaktadır.

LLVM ekosisteminin sunduğu modüler C++ kütüphaneleri, Clang ön yüzü, LLD bağlayıcısı ve ORC JIT altyapısı sayesinde oyun motorlarında yüksek performans, canlı betik derleme (Hot-Reloading) ve verimli bellek yönetimi elde edilebilmektedir. C++ tarafında `LLVMContext` bellek deposu, `Module` derleme konteyneri ve `IRBuilder` kod üreticisi nesneleri ile ilk fonksiyonumuz olan `Vec2Add` modülünü hafızada adım adım inşa edip doğruladık. Bir sonraki bölümde, dilimizin Ön Yüzünü (Frontend) inşa etmek üzere Soyut Sözdizim Ağaçları (AST), SSA formu ve LLVM bellek modelinin C++ ile adım adım uygulanmasını ele alacağız.
