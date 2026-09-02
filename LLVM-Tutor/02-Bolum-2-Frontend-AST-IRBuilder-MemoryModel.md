# Bölüm 2: Frontend Tasarımı, AST, LLVM IR Yapısı ve Bellek Modeli

## 2.1 Soyut Sözdizim Ağacı (AST) Tasarımı

Ön Yüz (Frontend) mimarisinin kalbinde **Soyut Sözdizim Ağacı (Abstract Syntax Tree - AST)** yer alır. AST, kaynak kodun metinsel halindeki gereksiz karakterleri (parantezler, noktalı virgüller, boşluklar vb.) ayıklayarak kodun mantıksal ve dilbilgisel hiyerarşisini ağaç veri yapısı şeklinde temsil eder. Oyun programlama dilimizde hem ifade (expression) hem de bildirim/deyim (statement) düğümlerine ihtiyacımız vardır.

Modern C++ (C++17/20) standartlarında AST düğümlerini `std::unique_ptr` ve sanal fonksiyonlar (polymorphism) ile temsil ederiz. Her AST düğümü, kendini LLVM IR'a dönüştürme sorumluluğuna (`codegen()`) sahiptir.

```
                  [FunctionAST: "UpdatePlayerPosition"]
                                   |
                         [BlockExprAST / Body]
                                   |
             +---------------------+---------------------+
             |                                           |
    [VarDeclAST: "pos"]                         [BinaryExprAST: "+="]
             |                                           |
    [Type: Vector3D]                    +----------------+----------------+
                                        |                                 |
                                 [VariableExprAST: "pos"]        [BinaryExprAST: "*"]
                                                                          |
                                                                  +-------+-------+
                                                                  |               |
                                                           [VariableAST]   [VariableAST]
                                                               "velocity"      "deltaTime"
```

---

## 2.2 LLVM IR (Intermediate Representation) Derinlemesine İnceleme

LLVM IR, LLVM derleyici altyapısının merkezindeki ortak dildir. Üç farklı biçimde temsil edilebilir:
1. **Hafızadaki (In-Memory) Veri Yapısı:** C++ sınıfları (`llvm::Instruction`, `llvm::Value`, `llvm::BasicBlock`).
2. **Disk Üzerindeki Okunabilir Metin Biçimi (.ll):** İnsanlar tarafından kolayca okunabilen montaj dili benzeri yapı.
3. **Disk Üzerindeki İkili Bitcode Biçimi (.bc):** Derleyicilerin hızlı işleyebilmesi için optimize edilmiş ikili format.

LLVM IR, güçlü bir şekilde tiplendirilmiş (strongly typed), sonsuz sayıda sanal yazmaca (virtual register) sahip ve **Tekli Atama Biçimine (Static Single Assignment - SSA)** dayalı bir yapıdır.

---

## 2.3 Tekli Atama Biçimi (Static Single Assignment - SSA) ve PHI Düğümleri

### SSA Nedir ve Neden Hayatidir?
SSA formunda, her sanal yazmaca veya değişkene **yalnızca bir kez** değer atanabilir. Klasik bir programlama dilinde bir değişkene birden fazla kez değer atanabilir:

```c
// Klasik C/C++ Kodu (Gereksiz Değişken Yeniden Atamaları)
int health = 100;
health = health - damage;
health = health + healAmount;
```

Bu kod SSA yapısına dönüştürüldüğünde, her atama yeni bir SSA sürüm değişkeni (register) oluşturur:

```llvm
; LLVM IR SSA Biçimi
%health0 = copy i32 100
%health1 = sub i32 %health0, %damage
%health2 = add i32 %health1, %healAmount
```

* **Çalışma Algoritması ve Avantajı:** SSA formunun derleyicilere sağladığı en büyük avantaj, **Veri Akışı Analizini (Data Flow Analysis)** doğrusal hale getirmesidir. Bir yazmacın değerinin nereden geldiği ve nerede değiştiği tartışmasız bir şekilde bellidir. Derleyici, karmaşık gösterici (pointer) takibi yapmadan bağımlılıkları görür.
* **Benzetme:** SSA formunu bir muhasebe defterindeki **Silinemez Günlük Kayıtlara (Immutable Ledger / Blockchain)** benzetebiliriz. Eski bir girdinin üzerini çizip değiştiremezsiniz; her yeni durum için yeni bir satır kaydı açarsınız.

### PHI Düğümleri (`phi` instruction) ve C++ ile Oluşturulması
Dallanma (if-else, döngüler) olan durumlarda bir değişkenin hangi kontrol akışı yolundan geldiği çalışma zamanında belli olur. SSA kuralını bozmadan bu durumu çözmek için **PHI Düğümleri (`phi` instruction)** kullanılır.

```llvm
; Oyuncu Can Durumu Örneği LLVM IR Çıktısı
entry:
  %cmp = icmp sgt i32 %damage, %shield
  br i1 %cmp, label %take_damage, label %blocked

take_damage:
  %hp_after_hit = sub i32 %current_hp, %damage
  br label %merge

blocked:
  %hp_after_block = select i1 true, i32 %current_hp, i32 0
  br label %merge

merge:
  ; PHI Düğümü: Kodun hangi bloktan geldiğine göre doğru hp değerini seçer
  %final_hp = phi i32 [ %hp_after_hit, %take_damage ], [ %hp_after_block, %blocked ]
  ret i32 %final_hp
```

#### PHI Düğümünün C++ API'si ile Adım Adım Kodlanması:
C++ tarafında bir PHI düğümünü oluşturup bağlamak son derece nettir:

```cpp
// 1. Adım: 'builder.CreatePHI' metodu ile PHI düğümü objemizi türetiyoruz.
// İlk parametre: Değişkenin tipi (32-bit tamsayı - i32).
// İkinci parametre: PHI düğümüne bağlanabilecek olası yol (incoming branches) sayısı (2 adet: take_damage ve blocked).
// Üçüncü parametre: LLVM IR çıktısındaki sanal yazmaç adı ("final_hp").
llvm::PHINode* phiNode = builder.CreatePHI(builder.getInt32Ty(), 2, "final_hp");

// 2. Adım: 'phiNode' objemize '.addIncoming' metodunu kullanarak yollardan gelen değerleri bağlıyoruz.
// 'take_damage' bloğundan gelinmişse '%hp_after_hit' değerini seç diyoruz:
phiNode->addIncoming(hpAfterHitVal, takeDamageBB);

// 3. Adım: 'blocked' bloğundan gelinmişse '%hp_after_block' değerini seç diyoruz:
phiNode->addIncoming(hpAfterBlockVal, blockedBB);
```

<div class="callout callout-warning">
<div class="callout-title">PHI Düğümü Kuralı</div>
LLVM IR içerisinde bir PHI düğümü talimatı (`phi`), bulunacağı Basic Block'un **en başında** (diğer tüm işlem talimatlarından önce) yer almak zorundadır!
</div>

---

## 2.4 Temel Yapı Taşları (Basic Blocks), Dallanmalar ve C++ İle Kontrol Akış Grafiği (CFG) İnşası

Bir **Temel Yapı Taşı (Basic Block)**, içerisine yalnızca tek bir noktadan girilen (ilk talimat) ve yalnızca tek bir noktadan çıkılan (son talimat) talimatlar dizisidir.

Bir Basic Block'un son talimatı mutlaka bir **Terminator Instruction** (Sonlandırıcı Talimat) olmalıdır. Örnek sonlandırıcılar:
* `ret`: Fonksiyondan döner.
* `br`: Başka bir Basic Block'a dallanır (koşullu veya koşulsuz).
* `switch`: Çoklu dallanma yapar.

### C++ Tarafında Koşullu Dallanma (If-Else) ve CFG İnşası Adım Adım:

Aşağıdaki C++ kod parçası, oyuncunun canı 0'dan büyükse oyuncunun yaşamaya devam ettiği (`alive`), aksi halde öldüğü (`dead`) kontrol akış grafiğini (CFG) inşa eder:

```cpp
// 1. Adım: Üç adet BasicBlock nesnesi oluşturuyoruz.
// Bunlar mantıksal dallanma rotalarımızdır.
llvm::BasicBlock* aliveBB = llvm::BasicBlock::Create(*context, "alive", playerFunc);
llvm::BasicBlock* deadBB  = llvm::BasicBlock::Create(*context, "dead", playerFunc);
llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "merge", playerFunc);

// 2. Adım: Oyuncu canını (health) 0 ile karşılaştıran 'icmp sgt' (Signed Greater Than) komutunu üretiyoruz.
// Dönen 'condValue' objesi i1 (boolean: true/false) tipindedir.
llvm::Value* condValue = builder.CreateICmpSGT(healthVal, builder.getInt32(0), "is_alive");

// 3. Adım: 'builder.CreateCondBr' metodu ile koşullu dallanma talimatı üretiyoruz.
// Eğer 'condValue' true ise 'aliveBB' bloğuna, false ise 'deadBB' bloğuna atlar.
builder.CreateCondBr(condValue, aliveBB, deadBB);

// 4. Adım: 'aliveBB' bloğunun içine giriyoruz ve imlecimizi oraya konumlandırıyoruz.
builder.SetInsertPoint(aliveBB);
// Hayatta kalma mantığı komutları...
builder.CreateBr(mergeBB); // İş bitince ortak birleşme bloğuna (mergeBB) koşulsuz atla.

// 5. Adım: 'deadBB' bloğunun içine giriyoruz.
builder.SetInsertPoint(deadBB);
// Ölme mantığı komutları...
builder.CreateBr(mergeBB); // İş bitince ortak birleşme bloğuna (mergeBB) koşulsuz atla.

// 6. Adım: Son olarak imlecimizi 'mergeBB' birleşme noktasına getiriyoruz.
builder.SetInsertPoint(mergeBB);
```

---

## 2.5 Bellek Modeli, Yığın Tahsisi (`alloca`) ve `GetElementPtr` (GEP) Adres Hesaplama Algoritması

LLVM IR'da iki temel bellek erişim mantığı vardır:
1. **Yazmaç Tabanlı (Register-based SSA):** `add`, `sub`, `mul` gibi işlemler doğrudan sanal yazmaçlar üzerinde yürür.
2. **Bellek Tabanlı (Memory-based Alloca):** Yerel değişkenler yığında (`alloca`) tutulur, `load` ile okunur, `store` ile yazılır.

### Yığın Tahsisi (`alloca`) ve C++ API Kullanımı
SSA formunu elle yönetmek zor olduğu için ön yüz derleyicileri genellikle tüm yerel değişkenleri fonksiyonun giriş bloğunda `alloca` ile oluşturur.

#### C++ Adım Adım `alloca` Oluşturulması:
```cpp
// 1. Adım: Fonksiyonun giriş bloğunda 32-bit tamsayı türünde 'player_health' için yığın alanı ayırıyoruz.
// Dönen 'healthAlloca' objesi bir Pointer (i32*) tipindedir.
llvm::AllocaInst* healthAlloca = builder.CreateAlloca(builder.getInt32Ty(), nullptr, "player_health");

// 2. Adım: 'builder.CreateStore' metodu ile yığındaki bu adrese ilk değer olarak 100 yazıyoruz.
builder.CreateStore(builder.getInt32(100), healthAlloca);

// 3. Adım: 'builder.CreateLoad' metodu ile yığındaki adresten değeri okuyup sanal bir yazmaca yüklüyoruz.
llvm::Value* currentHealth = builder.CreateLoad(builder.getInt32Ty(), healthAlloca, "current_health_val");
```

LLVM'in `mem2reg` optimizasyon passi (Pass Manager), bu `alloca` bellek erişimlerini otomatik olarak SSA yazmaçlarına ve PHI düğümlerine dönüştürür!

### `GetElementPtr` (GEP) Talimatı ve C++ İle Struct/Array Adres Hesaplaması
`GetElementPtr` (GEP), LLVM IR'ın en kritik ve sıkça yanlış anlaşılan talimatlarından biridir. GEP **belleğe erişmez (load/store yapmaz)**, yalnızca bellek adresini (pointer arithmetic) hesaplar!

Oyun motorumuzda bir `Entity` yapısının bellek düzenini düşünelim:

```cpp
struct Entity {
    int id;            // 0. eleman (4 bayt)
    float health;      // 1. eleman (4 bayt)
    float position[3]; // 2. eleman (12 bayt: x, y, z)
};
```

#### C++ Tarafında `Entity` Yapısının C++ LLVM API ile Oluşturulması ve GEP Kullanımı:

```cpp
// 1. Adım: C++ tarafında 'Entity' struct tipini LLVM'e tanıtıyoruz.
llvm::StructType* entityType = llvm::StructType::create(*context, "struct.Entity");

// Struct alan tipleri: i32 (id), float (health), [3 x float] (position dizisi)
llvm::ArrayType* posArrayType = llvm::ArrayType::get(builder.getFloatTy(), 3);
entityType->setBody({builder.getInt32Ty(), builder.getFloatTy(), posArrayType});

// 2. Adım: 'entityPtr' adında bir Entity* nesnesi olduğunu varsayalım (örn. fonksiyona parametre geldi).
// Oyuncunun position[1] (y koordinatı) adresini hesaplamak için GEP indekslerimizi hazırlıyoruz:
std::vector<llvm::Value*> indices = {
    builder.getInt32(0), // Indis 0: Pointer seviyesinde öteleme yok (entity_ptr[0])
    builder.getInt32(2), // Indis 2: Struct içindeki 2. alan (position dizisi)
    builder.getInt32(1)  // Indis 1: position dizisinin 1. elemanı (Y koordinatı)
};

// 3. Adım: 'builder.CreateGEP' metodu ile bellek adresi hesaplama talimatını üretiyoruz.
// İlk parametre struct tipi (entityType), ikinci parametre base pointer (entityPtr), üçüncü parametre indisler.
llvm::Value* yCoordPtr = builder.CreateGEP(entityType, entityPtr, indices, "y_coord_ptr");

// 4. Adım: Artık elde ettiğimiz 'yCoordPtr' adresinden değeri 'load' ile okuyabiliriz:
llvm::Value* yValue = builder.CreateLoad(builder.getFloatTy(), yCoordPtr, "y_val");
```

* **Adres Hesaplama Formülü:**
$$\text{Hedef Adres} = \text{Taban Adres} + (0 \times \text{sizeof}(Entity)) + \text{offsetof}(Entity, position) + (1 \times \text{sizeof}(float))$$

<div class="callout callout-info">
<div class="callout-title">GEP Neden İlk İndis Olarak '0' Alır?</div>
C/C++ dilindeki `entity_ptr->position[1]` ifadesinde, `entity_ptr` aslında bir dizi nesnesinin ilk elemanının adresidir (`*(entity_ptr + 0)`). İlk indis olan `0`, pointer seviyesinde kaç eleman öteye gidileceğini belirler.
</div>

---

## 2.6 `IRBuilder<>` Kullanımı ve AST'den LLVM IR Üretimi

Şimdi bu kavramları bir araya getirerek oyun dilimiz için tam teşekküllü bir AST yapısı ve bu AST'yi C++ LLVM API'si kullanarak adım adım LLVM IR'a dönüştüren derleyici kodunu yazalım.

### C++ Örneği: Tam Teşekküllü AST ve IR Generation (`src/frontend_ast.cpp`)

```cpp
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <string>
#include <vector>
#include <map>

// Derleyici Sembol Tablosu (Symbol Table): Değişken Adı -> Yığın Adresi (AllocaInst*)
static std::map<std::string, llvm::AllocaInst*> NamedValues;

// Taban AST Sınıfı
class ExprAST {
public:
    virtual ~ExprAST() = default;
    virtual llvm::Value* codegen(llvm::LLVMContext& context, llvm::Module& module, llvm::IRBuilder<>& builder) = 0;
};

// Sayısal Sabit AST Düğümü (Örn: 10.0, 2.5)
class NumberExprAST : public ExprAST {
    double val;
public:
    NumberExprAST(double val) : val(val) {}

    llvm::Value* codegen(llvm::LLVMContext& context, llvm::Module& module, llvm::IRBuilder<>& builder) override {
        // ConstantFP::get metodu ile kütüphanemizin merkezi deposundan float sabiti alıyoruz.
        return llvm::ConstantFP::get(context, llvm::APFloat(val));
    }
};

// Değişken Okuma AST Düğümü (Örn: playerHealth)
class VariableExprAST : public ExprAST {
    std::string name;
public:
    VariableExprAST(const std::string& name) : name(name) {}

    llvm::Value* codegen(llvm::LLVMContext& context, llvm::Module& module, llvm::IRBuilder<>& builder) override {
        // 1. Adım: Değişken adını sembol tablomuzda (NamedValues) arıyoruz.
        llvm::AllocaInst* alloca = NamedValues[name];
        if (!alloca) {
            llvm::errs() << "HATA: Tanımsız değişken kullanımı: " << name << "\n";
            return nullptr;
        }
        // 2. Adım: Değişken yığında bulunduğu için 'CreateLoad' ile adresindeki değeri okuyup döndürüyoruz.
        return builder.CreateLoad(alloca->getAllocatedType(), alloca, name.c_str());
    }
};

// İkili İşlem (Binary Expression: +, -, *) AST Düğümü
class BinaryExprAST : public ExprAST {
    char op;
    std::unique_ptr<ExprAST> lhs, rhs;
public:
    BinaryExprAST(char op, std::unique_ptr<ExprAST> lhs, std::unique_ptr<ExprAST> rhs)
        : op(op), lhs(std::move(lhs)), rhs(std::move(rhs)) {}

    llvm::Value* codegen(llvm::LLVMContext& context, llvm::Module& module, llvm::IRBuilder<>& builder) override {
        // Özyineli olarak sol ve sağ alt ağaçların IR kodlarını üretiyoruz.
        llvm::Value* l = lhs->codegen(context, module, builder);
        llvm::Value* r = rhs->codegen(context, module, builder);
        if (!l || !r) return nullptr;

        // İşlem operatörüne göre ilgili LLVM IR talimatını üretiyoruz.
        switch (op) {
            case '+': return builder.CreateFAdd(l, r, "addtmp");
            case '-': return builder.CreateFSub(l, r, "subtmp");
            case '*': return builder.CreateFMul(l, r, "multmp");
            default: return nullptr;
        }
    }
};

// Kullanım Örneği Fonksiyonu
void generateSampleAST() {
    // 1. LLVM Çekirdek Obje Kurulumları
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("GameASTModule", *context);
    llvm::IRBuilder<> builder(*context);

    // 2. Örnek İfade Ağacı Oluşturma: (10.0 + 20.0) * 2.5 (Oyun içi Hasar Çarpanı Hesabı)
    auto expr = std::make_unique<BinaryExprAST>(
        '*',
        std::make_unique<BinaryExprAST>('+', std::make_unique<NumberExprAST>(10.0), std::make_unique<NumberExprAST>(20.0)),
        std::make_unique<NumberExprAST>(2.5)
    );

    // 3. Fonksiyon İmzası Oluşturma: double CalculateDamage()
    llvm::FunctionType* ft = llvm::FunctionType::get(builder.getDoubleTy(), false);
    llvm::Function* func = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "CalculateDamage", module.get());
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", func);
    builder.SetInsertPoint(bb);

    // 4. AST Kod Üretimini Tetikleme
    llvm::Value* result = expr->codegen(*context, *module, builder);
    builder.CreateRet(result);

    // 5. Doğrulama ve Çıktı
    llvm::verifyFunction(*func);
    module->print(llvm::outs(), nullptr);
}
```

---

### Koda Adım Adım Derinlemesine Bakış ve Yürütme Algoritması

1. **`ExprAST::codegen` Sanal Fonksiyon Özyinelemesi (Recursion):**
   * *Çalışma Mantığı:* Ağacın en altındaki (leaf nodes) sabitler (`NumberExprAST`) önce ziyaret edilir. Alt düğümlerden dönen `llvm::Value*` göstericileri üst düğümlere (`BinaryExprAST`) girdi olarak iletilir. Bu yöntem Post-Order Traversal (Önce Sol, Sonra Sağ, Sonra Kök) algoritmasıdır.
2. **`builder.CreateFAdd` / `CreateFMul`:**
   * *Çalışma Mantığı:* İki float değerini toplayan veya çarpan LLVM IR talimatlarını oluşturur ve `addtmp`/`multmp` isimli sanal yazmaçlara atar.
3. **`builder.CreateRet(result)`:**
   * *Çalışma Mantığı:* Bloğun sonlandırıcı talimatını (Terminator) ekler ve hesaplanan nihai değeri fonksiyondan döndürür.

---

## 2.7 Bölüm Özeti ve Ön Yüz Mimarisi Değerlendirmesi

Bu bölümde, oyun programlama dilimizin Ön Yüz (Frontend) mimarisini baştan sona C++ LLVM API'lerini kullanarak ele aldık. Metinsel kaynak kodun **Soyut Sözdizim Ağacına (AST)** dönüştürülmesini, özyineli `codegen()` mimarisi ile bu ağacın LLVM IR'a aktarılmasını inceledik.

LLVM IR'ın bellek ve veri akışı omurgasını oluşturan **Static Single Assignment (SSA)** formunu, dallanmalardaki değer belirsizliklerini çözen **PHI Düğümlerini (`phi`)**, `CreateCondBr` ile kontrol akış grafiklerini (CFG) ve yığın tahsisi (`alloca`) ile bellek adres hesaplaması yapan **`GetElementPtr` (GEP)** talimatının C++ API ile adım adım kodlanmasını kavradık. Bir sonraki bölümde, ürettiğimiz bu ham LLVM IR'ı optimize etmek üzere Middle-End katmanına ve **New Pass Manager (NPM)** altyapısına geçeceğiz.
