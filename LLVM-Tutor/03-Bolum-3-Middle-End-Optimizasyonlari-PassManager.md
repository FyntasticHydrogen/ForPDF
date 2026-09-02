# Bölüm 3: Middle-end Optimizasyonları ve New Pass Manager (NPM)

## 3.1 LLVM Optimizasyon Mimarisi ve Pass Kavramı

LLVM derleyici altyapısının en güçlü yönlerinden biri modüler **Pass (Geçiş)** mimarisidir. Bir **Pass**, LLVM IR üzerinde belirli bir analizi (Analysis Pass) yürüten veya IR'ı değiştiren ve optimize eden (Transformation Pass) bağımsız bir C++ sınıfıdır.

Middle-end optimizasyon katmanı, kaynak dilden ve hedef donanımdan tamamen bağımsızdır. Amacı, ön yüzün ürettiği LLVM IR'ı matematiksel ve mantıksal olarak en sade, en hızlı ve en az bellek tüketen şekle getirmektir.

```
                      +-----------------------------+
                      |    Ham LLVM IR (Frontend)   |
                      +-----------------------------+
                                     |
                                     v
                      +-----------------------------+
                      |   Mem2Reg Pass (alloca->SSA)|
                      +-----------------------------+
                                     |
                                     v
                      +-----------------------------+
                      | Constant Folding & InstCombine |
                      +-----------------------------+
                                     |
                                     v
                      +-----------------------------+
                      |   CSE & Dead Code Elimination|
                      +-----------------------------+
                                     |
                                     v
                      +-----------------------------+
                      |    Loop Unrolling Pass      |
                      +-----------------------------+
                                     |
                                     v
                      +-----------------------------+
                      |  Optimize Edilmiş LLVM IR   |
                      +-----------------------------+
```

---

## 3.2 New Pass Manager (NPM) vs. Legacy Pass Manager

LLVM 13 ve sonrasında eski (Legacy) Pass Manager tamamen kaldırılmış ve yerini **New Pass Manager (NPM)** almıştır. New Pass Manager'ın getirdiği temel yenilikler ve avantajlar şunlardır:

1. **Tür Güvenliği ve Derleme Zamanı Şablonları (Templates & CRTP):** Kalıtım (vtable) yerine C++ şablonları ve Mixin yapısı (`llvm::PassInfoMixin`) kullanılarak sanal fonksiyon çağrı maliyetleri sıfırlanmıştır.
2. **Esnek Analiz Önbellekleme (Analysis Management):** Bir optimizasyon passi IR'ı değiştirmediyse, önceki analiz sonuçları (örneğin Dominator Tree, Loop Info) tekrar hesaplanmaz (`PreservedAnalyses::all()`).
3. **Piyasa Standartları Pipeline Desteği:** `O1`, `O2`, `O3`, `Os`, `Oz` gibi hazır optimizasyon seviyeleri tek hat üzerinden yapılandırılabilir.

<div class="callout callout-info">
<div class="callout-title">NPM vs Legacy Performans Farkı</div>
New Pass Manager, şablon tabanlı yapısı ve akıllı analiz önbellekleme (Analysis Caching) mekanizması sayesinde büyük projelerde derleme sürelerini %10 ile %25 arasında kısaltmaktadır.
</div>

---

## 3.3 Temel Optimizasyon Teknikleri ve IR Üzerinde İncelenmesi

Oyun motorlarında en yüksek kare hızlarına (FPS) ulaşmak için kritik öneme sahip 4 ana optimizasyon tekniğini inceleyelim:

### 1. Sabit Katlama (Constant Folding)
Derleme zamanında değeri bilinen sabit ifadelerin hesaplanarak tek bir değere indirgenmesidir.

* **Optimize Edilmemiş IR:**
```llvm
%a = fadd float 10.0, 20.0
%b = fmul float %a, 2.0
```

* **Constant Folding Sonrası IR:**
```llvm
%b = store float 60.0
```

### 2. Ortak Alt İfade Eleme (Common Subexpression Elimination - CSE)
Program içinde birden fazla kez tekrarlanan aynı matematiksel hesaplamaları tespit edip tek bir hesaplama sonucunu tekrar kullanmaktır. Oyun içi vektör uzaklık hesaplarında hayati önem taşır.

* **Ham IR:**
```llvm
%dx1 = fsub float %x2, %x1
%dy1 = fsub float %y2, %y1
%dx2 = fsub float %x2, %x1
```

* **CSE Sonrası IR:**
```llvm
%dx1 = fsub float %x2, %x1
%dy1 = fsub float %y2, %y1
```

### 3. Ölü Kod Eleme (Dead Code Elimination - DCE)
Programın çıktısını veya durumunu etkilemeyen, sonucu hiçbir yerde kullanılmayan (unreachable veya unused) talimatların silinmesidir.

* **Ham IR:**
```llvm
%unused_calc = fmul float %enemy_hp, 0.0
ret void
```

* **DCE Sonrası IR:**
```llvm
ret void
```

### 4. Döngü Açma (Loop Unrolling)
Döngü dallanma maliyetini (branch prediction ve sayaç artırma) azaltmak için döngü gövdesini ardışık olarak genişletmektir. Oyun motorlarında matris çarpmalarında ve SIMD vektörizasyonunda sıklıkla uygulanır.

---

## 3.4 Özel Bir LLVM Pass Yazımı (Modern C++ NPM)

Şimdi oyun derleyicimiz için custom bir LLVM Pass yazalım. Bu Pass, oyun kodumuzdaki tüm yavaş bölme işlemlerini (`fdiv`) çarpmaya (`fmul`) dönüştüren bir optimizasyon yapsın.

### C++ Kodu (`src/custom_pass.cpp`)

```cpp
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/raw_ostream.h>

class GameFastMathPass : public llvm::PassInfoMixin<GameFastMathPass> {
public:
    llvm::PreservedAnalyses run(llvm::Function& F, llvm::FunctionAnalysisManager& FAM) {
        bool changed = false;
        llvm::outs() << "GameFastMathPass Calisiyor: Fonksiyon " << F.getName() << "\n";

        for (auto& BB : F) {
            for (auto InstIt = BB.begin(); InstIt != BB.end(); ) {
                llvm::Instruction& I = *InstIt++;

                if (auto* fdiv = llvm::dyn_cast<llvm::BinaryOperator>(&I)) {
                    if (fdiv->getOpcode() == llvm::Instruction::FDiv) {
                        if (auto* c = llvm::dyn_cast<llvm::ConstantFP>(fdiv->getOperand(1))) {
                            double val = c->getValueAPF().convertToDouble();
                            if (val != 0.0) {
                                double recip = 1.0 / val;
                                llvm::IRBuilder<> builder(fdiv);
                                llvm::Value* recipVal = builder.getFloatTy()->isFloatTy() ?
                                    (llvm::Value*)llvm::ConstantFP::get(builder.getFloatTy(), recip) :
                                    (llvm::Value*)llvm::ConstantFP::get(builder.getDoubleTy(), recip);

                                llvm::Value* newMul = builder.CreateFMul(fdiv->getOperand(0), recipVal, "fastmul");
                                fdiv->replaceAllUsesWith(newMul);
                                fdiv->eraseFromParent();
                                changed = true;
                            }
                        }
                    }
                }
            }
        }

        return changed ? llvm::PreservedAnalyses::none() : llvm::PreservedAnalyses::all();
    }
};
```

<div class="callout callout-tip">
<div class="callout-title">İpucu: eraseFromParent() Kullanımı</div>
Bir LLVM talimatını silmeden önce (`eraseFromParent()`), onun ürettiği sonucu kullanan diğer tüm talimatların `replaceAllUsesWith()` ile yeni talimata yönlendirildiğinden emin olunmalıdır!
</div>

---

### Koda Adım Adım Derinlemesine Bakış ve Pass Yürütme Algoritması

1. **`PassInfoMixin<GameFastMathPass>` Kalıtımı:**
   * **Çalışma İlkesi:** Curiously Recurring Template Pattern (CRTP) yapısıdır. C++ vtable (sanal fonksiyon tablosu) masrafı olmadan static polymorphism sağlar ve derleme zamanı çağrı hızını artırır.
2. **`run(llvm::Function& F, llvm::FunctionAnalysisManager& FAM)` Metodu:**
   * **Çalışma İlkesi:** New Pass Manager, incelenen her fonksiyon için bu metodu otomatik yürütür. `FAM` parametresi sayesinde Dominator Tree veya Loop Info gibi analiz sonuçlarına anında erişilebilir.
3. **Güvenli İteratör Döngüsü (`InstIt++`):**
   * **Çalışma İlkesi:** Döngü içerisinde bir talimat silineceği için (`eraseFromParent()`), iteratör silme işleminden önce artırılır (`*InstIt++`). Eğer standart `for (auto& I : BB)` döngüsü kullanılsaydı, silinen eleman sonrası dangling pointer hatası meydana gelerek derleyici çökerdi.
4. **`dyn_cast<llvm::BinaryOperator>` ve `FDiv` Tespiti:**
   * **Çalışma İlkesi:** Talimatın ikili operatör olup olmadığı kontrol edilir. Ardından opcode değeri `llvm::Instruction::FDiv` ile kıyaslanarak float bölme işlemi tespiti yapılır.
5. **Sabit Bölen Kontrolü (`ConstantFP`) ve Ters Alma:**
   * **Çalışma İlkesi:** Bölme işleminin ikinci operandı (`getOperand(1)`) sabit bir kayan noktalı sayı ise (`llvm::ConstantFP`), bu sayı derleme zamanında double türüne dönüştürülür ve çarpmaya esas ters değeri (`1.0 / val`) hesaplanır.
6. **`IRBuilder<> builder(fdiv)` Yapılandırması:**
   * **Çalışma İlkesi:** Yeni çarpma talimatının, silinecek olan bölme talimatının tam konumuna yerleştirilmesi için `builder` nesnesi ilgili talimat konumuyla başlatılır.
7. **`replaceAllUsesWith` ve `eraseFromParent` Adımları:**
   * **Çalışma İlkesi:** `replaceAllUsesWith(newMul)` fonksiyonu, eski `fdiv` sonucunu girdi olarak kullanan tüm sonraki LLVM IR talimatlarını otomatik olarak yeni `newMul` sonucuna bağlar. Ardından `eraseFromParent()` ile eski `fdiv` talimatı temel bloktan ve bellekten güvenle silinir.
8. **`PreservedAnalyses` Dönüş Değeri:**
   * **Çalışma İlkesi:** Eğer fonksiyonda en az bir talimat değiştirildiyse `llvm::PreservedAnalyses::none()` dönülerek analizlerin sıfırlanması sağlanır. Hiçbir değişiklik yapılmadıysa `llvm::PreservedAnalyses::all()` dönülerek önceki analiz sonuçlarının önbellekte kalması sağlanır.

---

## 3.5 Pass Pipeline Oluşturma ve Optimizasyon Çalıştırma

Oyun derleyicimizde bu custom pass'ı ve LLVM'in standart optimizasyon pipeline'ını nasıl çalıştıracağımızı görelim:

```cpp
#include <llvm/Passes/PassBuilder.h>
#include <llvm/IR/PassManager.h>

void runOptimizationPipeline(llvm::Module& module) {
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    llvm::PassBuilder PB;

    PB.registerModuleAnalyses(MAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerRegisterClassBuilders(CGAM, LAM, FAM, MAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    llvm::ModulePassManager MPM;

    MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);

    MPM.run(module, MAM);
    llvm::outs() << "=== Modul Optimizasyonlari Tamamlandi ===\n";
}
```

---

### Pipeline Kodunun Adım Adım Açıklaması

1. **Analiz Yöneticilerinin Tanımlanması (`LAM`, `FAM`, `CGAM`, `MAM`):**
   * **Çalışma İlkesi:** Döngü (Loop), Fonksiyon (Function), Çağrı Grafiği (Call Graph) ve Modül (Module) seviyesindeki analizleri yönetmek ve önbelleğe almak için gerekli nesneler oluşturulur.
2. **`PassBuilder` Yapılandırması ve Analiz Kaydı:**
   * **Çalışma İlkesi:** `PB.registerModuleAnalyses(MAM)` ve ilgili kayıt fonksiyonları, analiz yöneticilerini birbiriyle ilişkilendirir ve proxy mekanizmasını aktifleştirir.
3. **`buildPerModuleDefaultPipeline` Seviye Seçimi:**
   * **Çalışma İlkesi:** LLVM'in varsayılan `O2` optimizasyon seviyesi boru hattı (pipeline) inşa edilir. Bu boru hattı içerisinde yüzlerce standart optimizasyon geçişi otomatik sıralamaya tabi tutulur.
4. **`MPM.run(module, MAM)` İcrası:**
   * **Çalışma İlkesi:** Tanımlanan ve yapılandırılan tüm optimizasyon pass'leri modül üzerindeki IR talimat dizilimlerine sırayla uygulanır.

---

## 3.6 Bölüm Özeti ve Orta Yüz (Middle-end) Optimizasyon Değerlendirmesi

Bu bölümde, derleyicimizin Orta Yüz (Middle-End) katmanını ve **New Pass Manager (NPM)** mimarisini detaylıca öğrendik. C++ şablonları tabanlı NPM yapısının eski Legacy Pass Manager'a göre sağladığı hız ve analiz önbellekleme üstünlüklerini inceledik.

Oyun motorlarının yüksek başarım gereksinimleri için hayati önem taşıyan Sabit Katlama (Constant Folding), Ortak Alt İfade Eleme (CSE), Ölü Kod Eleme (DCE) ve Döngü Açma (Loop Unrolling) tekniklerinin LLVM IR üzerindeki dönüşümlerini izledik. Yavaş bölme işlemlerini çarpmaya çeviren özgün bir `GameFastMathPass` C++ sınıfı kodlayarak analiz ve dönüştürme pass'lerinin çalışma algoritmalarını kavradık. Bir sonraki bölümde, optimize edilmiş bu IR'ı gerçek donanım komutlarına (Assembly / Object File) dönüştürmek üzere **Backend Kod Üretimi ve Target Machine** mimarisine geçeceğiz.
